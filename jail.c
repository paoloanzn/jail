/*
 * Copyright 2026 Paolo Anzani
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
#include <util.h>
#include <termios.h>

#define DYLD_PATH "/usr/lib/dyld"
#define CACHE_DIR "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"

#define DYLD_SHARED_CACHE_DIR_ENV "DYLD_SHARED_CACHE_DIR=" CACHE_DIR
#define DYLD_SHARED_REGION_ENV "DYLD_SHARED_REGION=private"

/* arm64e patching */
#define MH_MAGIC_64                  0xfeedfacf
#define FAT_MAGIC                    0xcafebabe
#define FAT_MAGIC_64                 0xcafebabf
#define CPU_TYPE_ARM64               0x0100000c
#define CPU_SUBTYPE_ARM64E_V0        0x80000002
#define CPU_SUBTYPE_ARM64E_V1        0x81000002
#define CPU_SUBTYPE_ARM64            0x00000000

#define LC_CODE_SIGNATURE            0x1d
#define CSMAGIC_EMBEDDED_SIGNATURE   0xfade0cc0
#define CSSLOT_SIGNATURESLOT         0x10000
#define CSSLOT_HIDDEN_CMS            0xfffe   /* unused slot type */

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

static int patch_thin_arm64e(unsigned char *base, size_t size) {
    /*
     *  mach_header_64 layout (32 bytes), little-endian:
     *    +0   magic          (uint32)  MH_MAGIC_64
     *    +4   cputype        (uint32)
     *    +8   cpusubtype     (uint32)  <-- patch this
     *    +12  filetype       (uint32)
     *    +16  ncmds          (uint32)
     *    ... up to +32 ...
     */
    static const size_t MACH_HEADER_64_SIZE        = 32;
    static const size_t MACH_HEADER_MAGIC_OFF      = 0;
    static const size_t MACH_HEADER_CPUTYPE_OFF    = 4;
    static const size_t MACH_HEADER_CPUSUBTYPE_OFF = 8;

    if (size < MACH_HEADER_64_SIZE) {
        return 0;
    }

    uint32_t magic = read_le32(base + MACH_HEADER_MAGIC_OFF);
    if (magic != MH_MAGIC_64) {
        return 0;
    }

    uint32_t cputype    = read_le32(base + MACH_HEADER_CPUTYPE_OFF);
    uint32_t cpusubtype = read_le32(base + MACH_HEADER_CPUSUBTYPE_OFF);

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

    write_le32(base + MACH_HEADER_CPUSUBTYPE_OFF, CPU_SUBTYPE_ARM64E_V1);
    /* signals patching has happened */
    return 1;
}

static int patch_arm64e_abi_v1(const char *path) {
    /*
     * return convention:
     * -1 error
     * 0 no patching
     * 1 file patched
     *
     *  fat_header (8 bytes), big-endian:
     *    +0   magic          (uint32)  FAT_MAGIC or FAT_MAGIC_64
     *    +4   nfat_arch      (uint32)  number of arch slices
     *    +8   fat_arch[]     (count = nfat_arch)
     *
     *  fat_arch entry — 20 bytes for FAT_MAGIC, 32 bytes for FAT_MAGIC_64:
     *    +0   cputype        (uint32)
     *    +4   cpusubtype     (uint32)
     *    +8   offset         (uint32 for FAT_MAGIC,
     *                         high 32 bits of uint64 for FAT_MAGIC_64)
     *    +12  ...            (low 32 bits of offset for FAT_MAGIC_64)
     *    ... size, align, reserved ...
     *
     *  The embedded Mach-O header at each slice's `offset` is at least
     *  MACH_HEADER_64_SIZE bytes (see patch_thin_arm64e).
     */
    static const size_t MACH_HEADER_64_SIZE        = 32;
    static const size_t FAT_HEADER_SIZE            = 8;
    static const size_t FAT_HEADER_NFAT_OFF        = 4;
    static const size_t FAT_ARCH_SIZE             = 20;
    static const size_t FAT_ARCH_64_SIZE          = 32;
    static const size_t FAT_ARCH_CPUTYPE_OFF      = 0;
    static const size_t FAT_ARCH_CPUSUBTYPE_OFF   = 4;
    static const size_t FAT_ARCH_OFFSET_OFF       = 8;
    static const size_t FAT_ARCH_64_OFFSET_LO_OFF = 12;

    int fd = -1;
    unsigned char *data = MAP_FAILED;
    size_t size = 0;
    int patched = 0;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "patch: open %s: %s\n", path, strerror(errno));
        goto fail;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "patch: fstat %s: %s\n", path, strerror(errno));
        goto fail;
    }

    /* min mach_header_64 size */
    if (st.st_size < (off_t)MACH_HEADER_64_SIZE) {
        close(fd);
        return 0;
    }

    size = (size_t)st.st_size;

    /**
     * map in memory with read and write enabled
     * MAP_SHARED -> map the changes to the file too
     */
    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "patch: mmap %s: %s\n", path, strerror(errno));
        goto fail;
    }

    uint32_t magic_le = read_le32(data);
    uint32_t magic_be = read_be32(data);

    /* if little endian */
    if (magic_le == MH_MAGIC_64) {
        patched = patch_thin_arm64e(data, size);
    } else if (magic_be == FAT_MAGIC || magic_be == FAT_MAGIC_64) {
        /* if universal binary (fat Mach-O) */

        if (size < FAT_HEADER_SIZE) goto fail;

        /*
         * nfat -> number of arch slices.
         * example: nfat = 2 -> slice 0: x86_64; slice 1: arm64;
         */
        uint32_t nfat = read_be32(data + FAT_HEADER_NFAT_OFF);

        size_t arch_table_size = (magic_be == FAT_MAGIC) ? FAT_ARCH_SIZE
                                                         : FAT_ARCH_64_SIZE;

        /* check the file size is AT LEAST as big as the table */
        if (FAT_HEADER_SIZE + ((size_t)nfat * arch_table_size) > size) {
            fprintf(stderr, "patch: malformed fat header: %s\n", path);
            goto fail;
        }

        for (uint32_t i = 0; i < nfat; i++) {
            unsigned char *arch_table_base =
                data + FAT_HEADER_SIZE + ((size_t)i * arch_table_size);

            uint32_t cputype    = read_be32(arch_table_base + FAT_ARCH_CPUTYPE_OFF);
            uint32_t cpusubtype = read_be32(arch_table_base + FAT_ARCH_CPUSUBTYPE_OFF);

            uint64_t offset;
            if (magic_be == FAT_MAGIC) {
                offset = read_be32(arch_table_base + FAT_ARCH_OFFSET_OFF);
            } else {
                uint32_t offset_hi = read_be32(arch_table_base + FAT_ARCH_OFFSET_OFF);
                uint32_t offset_lo = read_be32(arch_table_base + FAT_ARCH_64_OFFSET_LO_OFF);
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
            if (offset + MACH_HEADER_64_SIZE > size) {
                fprintf(stderr, "patch: bad Mach-O slice offset: %s\n", path);
                goto fail;
            }

            if (cpusubtype == CPU_SUBTYPE_ARM64E_V0) {
                /* patch the arch table */
                write_be32(arch_table_base + FAT_ARCH_CPUSUBTYPE_OFF,
                           CPU_SUBTYPE_ARM64E_V1);
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
        if (msync(data, size, MS_SYNC | MS_INVALIDATE) < 0) {
            fprintf(stderr, "patch: msync %s: %s\n", path, strerror(errno));
            goto fail;
        }
    }

    munmap(data, size);
    close(fd);
    return patched;

fail:
    if (data != MAP_FAILED) munmap(data, size);
    if (fd >= 0) close(fd);
    return -1;
}

/*
 * After `codesign --force --sign -`, the embedded signature still contains an
 * empty CMS blob wrapper (CSSLOT_SIGNATURESLOT, magic 0xfade0b01, length 8).
 *
 * AMFI's process_exec path (xnu bsd/kern/kern_exec.c) treats the binary as a
 * "simple adhoc signature" only when csblob_find_blob_bytes(SUPERBLOB,
 * CSSLOT_SIGNATURESLOT, CSMAGIC_BLOBWRAPPER) returns NULL. When it doesn't,
 * AMFI invalidates the vnode's cs_blob (clears CS_VALID) after the first exec.
 * The next exec then fails in load_code_signature with
 * "embedded signature doesn't match attached signature" because the cached
 * blob's CS_VALID is gone.
 *
 * Fix: rewrite the SuperBlob index entry's `type` field for CSSLOT_SIGNATURESLOT
 * to an unused value so the lookup misses. The CodeDirectory bytes (and thus
 * the CDHash) are untouched, so the adhoc signature stays valid.
 */
static int hide_cms_slot_in_slice(unsigned char *slice, size_t slice_size) {
    /*
     *  mach_header_64 layout (32 bytes):
     *    +16  ncmds          (uint32)
     *    +32  load commands start
     *
     *  Each load command (LC) header:
     *    +0   cmd            (uint32)  <- LC_CODE_SIGNATURE = 0x1d
     *    +4   cmdsize        (uint32)
     *
     *  LC_CODE_SIGNATURE (linkedit_data_command):
     *    +8   sig_offset     (uint32)  offset of signature data in slice
     *    +12  sig_size       (uint32)
     *
     *  CS_SuperBlob (12 bytes), big-endian:
     *    +0   magic          (uint32)  CSMAGIC_EMBEDDED_SIGNATURE
     *    +4   length         (uint32)
     *    +8   slot_count     (uint32)
     *    +12  CS_BlobIndex[slot_count]
     *
     *  CS_BlobIndex (8 bytes), big-endian:
     *    +0   type           (uint32)  <- CSSLOT_SIGNATURESLOT = 0x10000
     *    +4   offset         (uint32)
     */
    static const size_t MACH_HEADER_64_SIZE = 32;
    static const size_t MACH_HEADER_NCMDS_OFF = 16;
    static const size_t LC_DATA_OFFSET_OFF = 8;
    static const size_t LC_DATA_SIZE_OFF = 12;
    static const size_t SUPERBLOB_HEADER_SIZE = 12;
    static const size_t SUPERBLOB_COUNT_OFF = 8;
    static const size_t BLOB_INDEX_SIZE = 8;

    if (slice_size < MACH_HEADER_64_SIZE) return 0;
    if (read_le32(slice) != MH_MAGIC_64) return 0;

    uint32_t n_load_cmds = read_le32(slice + MACH_HEADER_NCMDS_OFF);
    /* load commands are immediately after mach_header_64 */
    size_t load_cmd_offset = MACH_HEADER_64_SIZE;
    int patched = 0;

    for (uint32_t lc_idx = 0; lc_idx < n_load_cmds; lc_idx++) {
        if (load_cmd_offset + 8 > slice_size) return -1;

        uint32_t load_cmd_type = read_le32(slice + load_cmd_offset);
        uint32_t load_cmd_size = read_le32(slice + load_cmd_offset + 4);

        if (load_cmd_type != LC_CODE_SIGNATURE) {
            /* load commands size is variable so we increment the offset after reading it. */
            load_cmd_offset += load_cmd_size;
            continue;
        }

        /* Found LC_CODE_SIGNATURE -> read the embedded SuperBlob */
        if (load_cmd_offset + 16 > slice_size) return -1;

        uint32_t superblob_offset = read_le32(slice + load_cmd_offset + LC_DATA_OFFSET_OFF);
        uint32_t superblob_size   = read_le32(slice + load_cmd_offset + LC_DATA_SIZE_OFF);

        if ((size_t)superblob_offset + SUPERBLOB_HEADER_SIZE > slice_size ||
            superblob_size < SUPERBLOB_HEADER_SIZE) return -1;

        unsigned char *superblob = slice + superblob_offset;
        if (read_be32(superblob) != CSMAGIC_EMBEDDED_SIGNATURE) return 0;
        uint32_t slot_count = read_be32(superblob + SUPERBLOB_COUNT_OFF);

        /* walk the BlobIndex array and rename any CSSLOT_SIGNATURESLOT entry. */
        for (uint32_t slot_idx = 0; slot_idx < slot_count; slot_idx++) {
            size_t blob_index_offset = SUPERBLOB_HEADER_SIZE +
                                       (size_t)slot_idx * BLOB_INDEX_SIZE;

            if (blob_index_offset + BLOB_INDEX_SIZE > superblob_size) return -1;

            /* check the slot type of BlobIndex[slot_idx] */
            uint32_t slot_type = read_be32(superblob + blob_index_offset);

            /* we change the slot type so it cannot be found by AMFI lookup. */
            if (slot_type == CSSLOT_SIGNATURESLOT) {
                write_be32(superblob + blob_index_offset, CSSLOT_HIDDEN_CMS);
                patched = 1;
            }
        }
        return patched;
    }
    return 0;
}

static int hide_cms_slot(const char *path) {
    int fd = -1;
    unsigned char *data = MAP_FAILED;
    size_t size = 0;
    int patched = 0;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "hide_cms: open %s: %s\n", path, strerror(errno));
        goto fail;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "hide_cms: fstat %s: %s\n", path, strerror(errno));
        goto fail;
    }
    if (st.st_size < 32) { close(fd); return 0; }

    size = (size_t)st.st_size;
    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "hide_cms: mmap %s: %s\n", path, strerror(errno));
        goto fail;
    }

    uint32_t magic_le = read_le32(data);
    uint32_t magic_be = read_be32(data);

    if (magic_le == MH_MAGIC_64) {
        int r = hide_cms_slot_in_slice(data, size);
        if (r < 0) goto fail;
        patched |= r;
    } else if (magic_be == FAT_MAGIC || magic_be == FAT_MAGIC_64) {
        uint32_t nfat = read_be32(data + 4);
        size_t arch_table_size = (magic_be == FAT_MAGIC) ? 20 : 32;
        if (8 + ((size_t)nfat * arch_table_size) > size) goto fail;
        for (uint32_t i = 0; i < nfat; i++) {
            unsigned char *at = data + 8 + ((size_t)i * arch_table_size);
            uint64_t offset, slice_size;
            if (magic_be == FAT_MAGIC) {
                offset = read_be32(at + 8);
                slice_size = read_be32(at + 12);
            } else {
                offset = ((uint64_t)read_be32(at + 8) << 32) | read_be32(at + 12);
                slice_size = ((uint64_t)read_be32(at + 16) << 32) | read_be32(at + 20);
            }
            if (offset + slice_size > size) goto fail;
            int r = hide_cms_slot_in_slice(data + offset, (size_t)slice_size);
            if (r < 0) goto fail;
            patched |= r;
        }
    }

    if (patched) {
        if (msync(data, size, MS_SYNC | MS_INVALIDATE) < 0) {
            fprintf(stderr, "hide_cms: msync %s: %s\n", path, strerror(errno));
            goto fail;
        }
    }
    munmap(data, size);
    close(fd);
    return patched;

fail:
    if (data != MAP_FAILED) munmap(data, size);
    if (fd >= 0) close(fd);
    return -1;
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

static int cmd_bootstrap(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: bootstrap <rootfs>\n");
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
        fprintf(stderr, "usage: run <rootfs> <binary_path> [args...]\n");
        return 2;
    }

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
        char *cenvp[] = { DYLD_SHARED_CACHE_DIR_ENV, DYLD_SHARED_REGION_ENV, NULL };

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
        fprintf(stderr, "usage: install <rootfs> <host-path> <rootfs-path>\n");
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
    if(patched > 0) {
        resign_adhoc(dst);
        /* Hide the empty CMS slot left by codesign so AMFI accepts the
         * binary as a "simple adhoc signature" and does not invalidate the
         * vnode's cs_blob after the first exec. */
        if (hide_cms_slot(dst) < 0) die("hide cms slot");
    }

    /* set execution flag */
    if (chmod(dst, 0755) < 0) die("chmod");
    return 0;
}

static struct termios saved_tios;
static int saved_tios_valid = 0;

static void enter_raw_mode(void) {
    if (!isatty(0)) return;
    /*
     * save current termios configuration for late restoring.
     * never exit the program leaving the user's terminal in raw mode.
     */
    if (tcgetattr(0, &saved_tios) < 0) die("tcgetattr");

    saved_tios_valid = 1;
    struct termios raw = saved_tios;
    cfmakeraw(&raw);

    if (tcsetattr(0, TCSANOW, &raw) < 0) die("tcsetattr raw");
}

static void leave_raw_mode(void) {
    /* restore original termios configuration */
    if (saved_tios_valid) {
        tcsetattr(0, TCSANOW, &saved_tios);
    }
}

static void drain_to_stdout(int master) {
    /* after child exit master still has buffered bytes */
    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    char buf[4096]; ssize_t n;
    while ((n = read(master, buf, sizeof buf)) > 0) {
        write(1, buf, n);
    }
    fcntl(master, F_SETFL, flags);
}

static void relay(int master) {
    /* file descriptors set */
    fd_set rfds; char buf[4096];
    int stdin_open = 1;
    /*
     * loop keeps forwarding bytes in both directions:
     * pty master -> host stdout
     * host stdin -> pty master
     */
    for(;;) {
        /* clear the set */
        FD_ZERO(&rfds);

        /* 
         * master -> readable when the shell wrote output to the pty
         * 0 (stdin) -> readable when the user typed input into the runner
         */
        FD_SET(master, &rfds);
        if (stdin_open) FD_SET(0, &rfds);

        int maxfd = master > 0 ? master : 0;
        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue; die("select");
        }

        if (FD_ISSET(master, &rfds)) {
            ssize_t n = read(master, buf, sizeof buf);
            if (n <= 0) {
                drain_to_stdout(master);
                return;
            }
            write(1, buf, n);
        }
        if (stdin_open && FD_ISSET(0 ,&rfds)) {
            ssize_t n = read(0, buf, sizeof buf);
            /* user closed stdin -> send pty EOF and keep draining output */
            if (n <= 0) {
                char eof = 4;
                write(master, &eof, 1);
                stdin_open = 0;
                continue;
            }
            write(master, buf, n);
        }
    }
}

static int cmd_shell(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: shell <rootfs> <shellbin-path>\n");
        return 2;
    }

    const char *rootfs = argv[1];
    const char *shellbin = argv[2];

    int master;
    pid_t pid = forkpty(&master, NULL, NULL, NULL);
    if (pid < 0) die("forkpty");

    if (pid == 0) {
        if (chdir(rootfs) < 0)  die("chdir");
        if (chroot(".") < 0)    die("chroot");
        if (chdir("/") < 0)     die("chdir post-chroot");

        char *cenvp[] = { DYLD_SHARED_CACHE_DIR_ENV, DYLD_SHARED_REGION_ENV, NULL };
        execve(shellbin, argv+2, cenvp);
        die("execve");
    }

    /* put parent terminal in raw mode so keystrokes flow to the pty unmodified */
    enter_raw_mode();
    atexit(leave_raw_mode);
    /* start master - replica pty relay loop */
    relay(master);
    int status; waitpid(pid, &status, 0) ;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s bootstrap <rootfs>\n"
            "       %s run <rootfs> <binpath> [args...]\n"
            "       %s install <rootfs> <host-path> <rootfs-path>\n"
            "       %s shell <rootfs> <shellbin-path> [args...]\n",
            argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "bootstrap") == 0) {
        return cmd_bootstrap(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "shell") == 0) {
        return cmd_shell(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "install") == 0) {
        return cmd_install(argc - 1, argv + 1);
    }
    fprintf(stderr, "%s: unknow subcommand %s\n", argv[0], argv[1]);
    return 2;
}
