#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DYLD_PATH "/usr/lib/dyld"
#define CACHE_DIR "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"

static void die(const char *what) {
    fprintf(stderr, "jail: %s: %s\n", what, strerror(errno));
    exit(1);
}

static void copyfile(const char *src, const char *dst) {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "cp -f '%s' '%s'", src, dst);
    if(system(cmd) != 0) die("copy");
}

static void mkdir_p(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
    if(system(cmd) != 0) die("mkdir_p");
}

static int cmd_bootstrap(int argc, char **argv);
static int cmd_run(int argc, char **argv);
static int cmd_install(int argc, char **argv);

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, 
            "usage: %s bootstrap <rootfs>\n"
            "       %s run <rootfs> <binpath> [args...]\n"
            "       %s install <rootfs> <host-path> <rootfs-path>\n",
            argv[0], argv[0], argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "bootstrap") == 0) {
        return cmd_bootstrap(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "install") == 0) {
        return cmd_install(argc - 1, argv + 1);
    }
    fprintf(stderr, "%s: unknow subcommand %s\n", argv[0], argv[1]);
    return 2;
}

static int cmd_bootstrap(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s bootstrap <rootfs>\n", argv[0]);
        return 2;
    }

    const char *rootfs = argv[1];

    /* build minimal rootfs folder structure */
    char buf[4096];
    snprintf(buf, sizeof buf, "%s/usr/lib", rootfs); mkdir_p(buf);
    snprintf(buf, sizeof buf, "%s/bin", rootfs); mkdir_p(buf);
    snprintf(buf, sizeof buf, "%s%s", rootfs, CACHE_DIR); mkdir_p(buf);
    snprintf(buf, sizeof buf, "%s%s", rootfs, DYLD_PATH); copyfile(DYLD_PATH, buf);

    /* copy shared cache -> all shared libs are here */
    snprintf(buf, sizeof buf, "cp -f %s/dyld_shared_cache_arm64e* %s%s", CACHE_DIR, rootfs, CACHE_DIR);
    if (system(buf) != 0) die("copy cache");

    return 0;
}

static int cmd_run(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s run <rootfs> <binary_path> [args...]\n", argv[0]);
        return 2;
    }

    char buf[4096];

    const char *rootfs = argv[1];
    const char *binpath = argv[2];

    /* fork, chroot in the child, exec binary */
    pid_t pid = fork();
    if (pid < 0) die("fork");

    if (pid == 0) {
        if (chdir(rootfs) < 0) die("chdir");
        if (chroot(".") < 0) die("chroot");
        if (chdir("/") < 0) die("chdir post-chroot");
        
        /*
         * dyld inside the chroot:
         *   <binary> -> /usr/lib/dyld -> libSystem -> dyld shared cache
         *
         * The cache files were copied into the rootfs, but not with full system
         * metadata. In particular, cp -p cannot preserve Apple's SIP/rootless flags
         * here: it eventually needs chflags(SF_RESTRICTED) on the copied files, and
         * XNU blocks that for normal userland, even as root.
         *
         * Normal dyld cache mapping:
         *   copied cache -> XNU shared-region mapping
         *                -> XNU expects a trusted/SIP-protected cache vnode
         *                -> copied cache is not equivalent to the real Apple vnode
         *                -> "syscall to map cache into shared region failed"
         *
         * Workaround:
         *   DYLD_SHARED_CACHE_DIR
         *     -> tell dyld where the copied cache lives inside the chroot
         *
         *   DYLD_SHARED_REGION=private
         *     -> avoid the global shared-region path
         *     -> privately mmap this process's copy of the cache instead
         */
        snprintf(buf, sizeof buf, "DYLD_SHARED_CACHE_DIR=%s", CACHE_DIR);
        char *cenvp[] = { buf, "DYLD_SHARED_REGION=private", NULL };

        execve(binpath, argv + 2, cenvp);
        die("execve");
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) die("waitpid");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int cmd_install(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s install <rootfs> <host-path> <rootfs-path>\n", argv[0]);
        return 2;
    }

    const char *rootfs = argv[1];
    const char *host = argv[2];
    const char *inside = argv[3];

    char dst[4096]; char parent[4096];
    snprintf(dst, sizeof dst, "%s%s", rootfs, inside);

    /* check if parent exists */
    strncpy(parent, dst, sizeof parent - 1);
    parent[sizeof parent - 1] = 0;

    /* return ptr to the last occurrence of '/' in parent */
    /* parent = '/foo/bar/file.txt' -> ptr to '/' of '/file.txt'
     */
    char *slash = strrchr(parent, '/');
    if (slash) {
        /* parent = '/foo/bar/file.txt'  -> parent = '/foo/bar' */
        *slash = 0;
        mkdir_p(parent);
    }

    copyfile(host, dst);
    /* set execution flag */
    if (chmod(dst, 0755) < 0) die("chmod");
    return 0;
}