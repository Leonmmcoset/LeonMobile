#define _GNU_SOURCE
#include "namespace.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/* Only this root-owned entry exists outside the private mount namespace. Each
 * launch overlays it with a different tmpfs; no app can reach the staging tree. */
#define ROOT "/run/leon-sandbox"
#define SAFE (MS_NOSUID | MS_NODEV)
static int target(char *out, const char *path) {
    if (!path || path[0] != '/' || strstr(path, "/../") || strstr(path, "/./") ||
        snprintf(out, PATH_MAX, "%s%s", ROOT, path) >= PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
static int directories(const char *path) {
    char full[PATH_MAX];
    if (target(full, path)) return -1;
    for (char *p = full + sizeof(ROOT); *p; ++p)
        if (*p == '/') {
            *p = 0;
            int result = mkdir(full, 0755);
            *p = '/';
            if (result && errno != EEXIST) return -1;
        }
    return mkdir(full, 0755) && errno != EEXIST ? -1 : 0;
}
static int bind_directory(const char *path, int readonly, int private, uid_t uid, int optional) {
    int fd = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC | (private ? O_NOFOLLOW : 0));
    if (fd < 0) return optional && errno == ENOENT ? 0 : -1;
    struct stat info;
    int result = -1;
    char source[64], full[PATH_MAX];
    if (fstat(fd, &info)) goto done;
    if (private && (info.st_uid != uid || info.st_gid != uid || (info.st_mode & 0077))) {
        errno = EPERM;
        goto done;
    }
    snprintf(source, sizeof(source), "/proc/self/fd/%d", fd);
    if (directories(path) || target(full, path) || mount(source, full, NULL, MS_BIND, NULL) ||
        mount(NULL, full, NULL, MS_BIND | MS_REMOUNT | SAFE | (readonly ? MS_RDONLY : 0), NULL))
        goto done;
    result = 0;
done:
    close(fd);
    return result;
}
static int temporary(const char *path, const char *options, unsigned long flags) {
    char full[PATH_MAX];
    if (directories(path) || target(full, path)) return -1;
    return mount("tmpfs", full, "tmpfs", MS_NOSUID | MS_NOEXEC | flags, options);
}
static int link_path(const char *source, const char *path) {
    char full[PATH_MAX];
    return target(full, path) ? -1 : symlink(source, full);
}
static int devices(uid_t uid) {
    if (temporary("/dev", "mode=0755,size=1m,nr_inodes=256", 0) || directories("/dev/pts"))
        return -1;
    const struct { const char *name; int major, minor; } nodes[] = {
        {"null", 1, 3}, {"zero", 1, 5}, {"full", 1, 7},
        {"random", 1, 8}, {"urandom", 1, 9}, {"tty", 5, 0}};
    for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), ROOT "/dev/%s", nodes[i].name);
        if (mknod(path, S_IFCHR | 0666, makedev(nodes[i].major, nodes[i].minor)) || chmod(path, 0666))
            return -1;
    }
    char options[128];
    snprintf(options, sizeof(options), "newinstance,ptmxmode=0666,mode=0600,max=64,gid=%u", uid);
    if (mount("devpts", ROOT "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, options) ||
        link_path("pts/ptmx", "/dev/ptmx") || link_path("/proc/self/fd", "/dev/fd") ||
        link_path("/proc/self/fd/0", "/dev/stdin") ||
        link_path("/proc/self/fd/1", "/dev/stdout") ||
        link_path("/proc/self/fd/2", "/dev/stderr") ||
        temporary("/dev/shm", "mode=1777,size=32m,nr_inodes=4096", MS_NODEV))
        return -1;
    return 0;
}
int leon_namespace_filesystem(const char *home, const char *runtime, const char *package, uid_t uid) {
    struct stat staging;
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
        (mkdir(ROOT, 0700) && errno != EEXIST) || lstat(ROOT, &staging))
        return -1;
    if (!S_ISDIR(staging.st_mode) || staging.st_uid || (staging.st_mode & 0077)) {
        errno = EPERM;
        return -1;
    }
    if (mount("tmpfs", ROOT, "tmpfs", SAFE, "mode=0755,size=4m,nr_inodes=2048"))
        return -1;
    const char *system[] = {"/usr", "/bin", "/sbin", "/lib", "/etc", NULL};
    for (size_t i = 0; system[i]; ++i)
        if (bind_directory(system[i], 1, 0, 0, 0)) return -1;
    if (bind_directory("/lib64", 1, 0, 0, 1) ||
        bind_directory(home, 0, 1, uid, 0) || bind_directory(runtime, 0, 1, uid, 0) ||
        (package && *package && bind_directory(package, 1, 0, 0, 0)))
        return -1;
    /* Directory binds keep atomic socket, resolver and timezone replacements
     * visible, without exposing the host's other runtime or data directories. */
    const char *services[] = {"/run/leon", "/run/leon-root", "/run/leon-auth", "/run/leon-log",
                              "/run/leon-system", NULL};
    for (size_t i = 0; services[i]; ++i)
        if (bind_directory(services[i], 1, 0, 0, 0)) return -1;
    if (link_path("leon-system/resolv.conf", "/run/resolv.conf") ||
        link_path("leon-system/localtime", "/run/localtime") ||
        link_path("/proc/self/mounts", "/run/mtab") ||
        directories("/sys") || directories("/proc") || devices(uid) ||
        temporary("/tmp", "mode=1777,size=32m,nr_inodes=8192", MS_NODEV) ||
        temporary("/var/tmp", "mode=1777,size=16m,nr_inodes=4096", MS_NODEV) ||
        mount("proc", ROOT "/proc", "proc", SAFE | MS_NOEXEC | MS_RDONLY, NULL) ||
        mount(NULL, ROOT, NULL, MS_REMOUNT | SAFE | MS_RDONLY, NULL) ||
        chdir(ROOT) || chroot(".") || chdir("/"))
        return -1;
    return 0;
}
