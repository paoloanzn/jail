#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#define DYLD_PATH "/usr/lib/dyld"
#define CACHE_DIR "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"

/* arm64e patching */
#define MH_MAGIC_64                  0xfeedfacf
#define FAT_MAGIC                    0xcafebabe
#define FAT_MAGIC_64                 0xcafebabf
#define CPU_TYPE_ARM64               0x0100000c
#define CPU_SUBTYPE_ARM64E_V0        0x80000002
#define CPU_SUBTYPE_ARM64E_V1        0x81000002

/*
 *  arm64 only binaries -> Mach-O Thin -> little endian byte order
 *  universal binaries (arm64 + x86) -> Mach-O Fat -> big endian byte order
 */

static uint32_t read_le32(const unsigned char *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24); 
}

static uint32_t read_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void write_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void write_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xff);
    p[1] = (unsigned char)((v >> 16) & 0xff);
    p[2] = (unsigned char)((v >> 8) & 0xff);
    p[3] = (unsigned char)(v & 0xff);
}

/*
 *  mach_header_64
 *  uint32_t magic;           // offset 0
 *  cpu_type_t cputype;       // offset 4
 *  cpu_subtype_t cpusubtype; // offset 8  <-- patch this
 */

static int patch_thin_arm64e(unsigned char *base, size_t size) {
    /* mach_header_64 is at least 32 bytes long */
    if (size < 32) {
        return 0;
    }

    uint32_t magic = read_le32(base + 0);
    if (magic != MH_MAGIC_64) {
        return 0;
    }

    uint32_t cputype = read_le32(base + 4);
    uint32_t cpusubtype = read_le32(base + 8);

    if (cputype != CPU_TYPE_ARM64) {
        return 0;
    }

    if (cpusubtype == CPU_SUBTYPE_ARM64E_V1) {
        /* already patched */
        return 0;
    }

    if (cpusubtype != CPU_SUBTYPE_ARM64E_V0) {
        /* not the thing we need to patch */
        return 0;
    }

    write_le32(base + 8, CPU_SUBTYPE_ARM64E_V1);
    /* signals patching has happened */
    return 1;
}

static int patch_arm64e_abi_v1(const char *path) {
    /*
     * return convention:
     * -1 error
     * 0 no patching
     * 1 file patched 
     */

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "patch: open %s: %s\n", path, strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "patch: fstat %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    /* min mach_header_64 size */
    if (st.st_size < 32) {
        close(fd);
        return 0;
    }

    /**
     * map in memory with read and write enabled
     * MAP_SHARED -> map the changes to the file too
     */
    unsigned char *data = mmap(
                        NULL, (size_t)st.st_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0
                    );
    if (data == MAP_FAILED) {
        fprintf(stderr, "patch: mmap %s: %s\n", path, strerror(errno));
        close(fd);
        return -1; 
    }

    /* store file size and track modifications */
    size_t size = (size_t)st.st_size;
    int patched = 0;

    uint32_t magic_le = read_le32(data);
    uint32_t magic_be = read_be32(data);

    /* if little endian */
    if (magic_le == MH_MAGIC_64) {
        patched = patch_thin_arm64e(data, size);
    } else if (magic_be == FAT_MAGIC || magic_be == FAT_MAGIC_64) {
        /* if universal binary (fat Mach-O) */

        /* fat header has at least 8 bytes */
        if (size < 8) {
            munmap(data, size);
            close(fd);
            return -1;
        }

        /*
         * at byte 4 of the header stores the number of arch slices
         * example: nfat = 2 -> slice 0: x86_64; slice 1: arm64;
         */
        uint32_t nfat = read_be32(data + 4);
       
        /*
         * FAT_MAGIC -> fat_arch -> 20 bytes
         * cputype      4 bytes
         * cpusubtype   4 bytes     --> offset 4
         * offset       4 bytes     --> offset 8 --> 32-bit int
         * size         4 bytes
         * align        4 bytes
         * 
         * FAT_MAGIC_64 -> fat_arch_64 -> 32 bytes
         * cputype      4 bytes
         * cpusubtype   4 bytes     --> offset 4
         * offset       8 bytes     --> offset 8 --> 64-bit int
         * size         8 bytes
         * align        4 bytes
         * reserved     4 bytes
         */
        size_t arch_table_size = (magic_be == FAT_MAGIC) ? 20 : 32;

        /* check the file size is AT LEAST as big as the table (starts at offset 8) */
        if (8 + ((size_t)nfat * arch_table_size) > size) {
            fprintf(stderr, "patch: malformed fat header: %s\n", path);
            munmap(data, size);
            close(fd);
            return -1;
        }

        for (uint32_t i = 0; i < nfat; i++) {
            unsigned char *arch_table_base = data + 8 + ((size_t)i * arch_table_size);

            uint32_t cputype = read_be32(arch_table_base + 0);
            uint32_t cpusubtype = read_be32(arch_table_base + 4);
            
            uint64_t offset;

            if (magic_be == FAT_MAGIC) {
                offset = read_be32(arch_table_base + 8);
            } else {
                uint32_t offset_hi = read_be32(arch_table_base + 8);
                uint32_t offset_lo = read_be32(arch_table_base + 12);
                offset = ((uint64_t)offset_hi << 32) | offset_lo;
            }
            
            if (cputype != CPU_TYPE_ARM64) {
                continue;
            }

            if (cpusubtype != CPU_SUBTYPE_ARM64E_V0 &&
                cpusubtype != CPU_SUBTYPE_ARM64E_V1) {
                /* not the thing we need to patch */
                continue;
            }

            /* verify there is AT LEAST the embedded Mach-O header at offset */
            if (offset + 32 > size) {
                fprintf(stderr, "patch: bad Mach-O slice offset: %s\n", path);
                munmap(data, size);
                close(fd);
                return -1;
            }

            if (cpusubtype == CPU_SUBTYPE_ARM64E_V0) {
                /* patch the arch table */
                write_be32(arch_table_base + 4, CPU_SUBTYPE_ARM64E_V1);
                patched = 1;
            }

            /* patch the inner Mach-O header */
            if (patch_thin_arm64e(data + offset, size - (size_t)offset)) {
                patched = 1;
            }

        }
    }

    /* flush modifications in memory to the file */
    if (patched) {
        if (msync(data, size, MS_ASYNC) < 0) {
            fprintf(stderr, "patch: msync %s: %s\n", path, strerror(errno));
            munmap(data, size);
            close(fd);
            return -1;
        }
    }

    munmap(data, size);
    close(fd);
    return patched;
}

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

static void resign_adhoc(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "codesign --remove-signature '%s' 2>/dev/null || true", path);
    if(system(cmd) != 0) die("codesign remove-signature");

    snprintf(cmd, sizeof cmd, "codesign --force --sign - '%s'", path);
    if(system(cmd) != 0) die("codesign sign adhoc");
}

static void close_fds_from(int start_fd) {
    for (int fd = start_fd; fd < 1024; fd++) close(fd);
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
    if (argc < 3) {
        fprintf(stderr, "usage: %s run <rootfs> <binary_path> [args...]\n", argv[0]);
        return 2;
    }

    /* NOTE: this is used also in the child process, might creat leak risks */
    char buf[4096];

    const char *rootfs = argv[1];
    const char *binpath = argv[2];

    int out_pipe[2];
    if (pipe(out_pipe) < 0) die("pipe");

    /* fork, chroot in the child, exec binary */
    pid_t pid = fork();
    if (pid < 0) die("fork");

    if (pid == 0) {
        /* redirect stdout and stderr to pipe write end */
        if (dup2(out_pipe[1], 1) < 0) die("dup2 stdout");
        if (dup2(out_pipe[1], 2) < 0) die("dup2 stderr");
        /* close original parent's fd to pipe ends*/
        close(out_pipe[0]); close(out_pipe[1]);

        /* close all other inherithed fds */
        close_fds_from(3);

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

    close(out_pipe[1]);
    /* read child stdout and stderr from pipe */
    ssize_t n;
    while ((n = read(out_pipe[0], buf, sizeof buf)) > 0) {
        write(1, buf, n);
    }
    close(out_pipe[0]);

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

    /* try to patch and resign the file */
    int patched = patch_arm64e_abi_v1(dst);
    if(patched < 0) die("patch arm64e");
    if(patched > 0) resign_adhoc(dst);

    /* set execution flag */
    if (chmod(dst, 0755) < 0) die("chmod");
    return 0;
}
