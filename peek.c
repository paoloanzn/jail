#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

static void try_stat(const char *p) {
    struct stat st;
    if (stat(p, &st) == 0) {
        printf("SEE %s (mode=%o size=%lld)\n", 
        p, st.st_mode & 0777, (long long)st.st_size);
    } else {
        printf("MISS %s\n", p);
    }
}

static void list(const char *p) {
    DIR *d = opendir(p);
    if (!d) { printf("MISS dir %s\n", p); return; }

    printf("DIR %s\n", p);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] != '.') { printf("     %s\n", e->d_name); }
    }
    closedir(d);
}

int main(void) {
    list("/");
    try_stat("/bin/peek");
    try_stat("/etc/hosts");
    try_stat("/Users");
    try_stat("/usr/lib/dyld"); 
    return 0;
}