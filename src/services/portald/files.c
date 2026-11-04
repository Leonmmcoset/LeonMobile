#include "daemon.h"
#include "service.h"
#include <errno.h>
#include <fcntl.h>
/* Linux openat2 UAPI, kept local for the pinned musl SDK's older headers. */
struct open_how {
    uint64_t flags, mode, resolve;
};
#define RESOLVE_NO_SYMLINKS 0x04
#define RESOLVE_NO_MAGICLINKS 0x02
#define RESOLVE_BENEATH 0x08
#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <dirent.h>
lm_json *portal_import(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct portal *s = d->data;

    if (!lm_trusted(uid))
        return lm_failure("permission denied");
    long long target_uid = lm_integer(lm_get(r, "uid"), -1);
    if (target_uid < 11000 || target_uid > 60000)
        return lm_failure("invalid application UID");
    lm_json *app = lm_app(d->options.root, d->options.builtin, (uid_t)target_uid);
    if (!app || !portal_granted(s, app, "files.import")) {
        lm_free(app);
        return lm_failure("file import is not authorized");
    }
    const char *relative = lm_string(lm_get(r, "file"), "");
    if (!*relative || relative[0] == '/' || strlen(relative) > 240) {
        lm_free(app);
        return lm_failure("expected a relative shared-file path");
    }
    char shared[PATH_MAX];
    snprintf(shared, sizeof(shared), "%s/shared", d->options.root);
    int base = open(shared, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct open_how how = {.flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC,
                           .resolve =
                               RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS};
    int source = base < 0 ? -1 : (int)syscall(SYS_openat2, base, relative, &how, sizeof(how));
    if (base >= 0)
        close(base);
    struct stat info;
    if (source < 0 || fstat(source, &info) || !S_ISREG(info.st_mode) ||
        info.st_size > 16 * 1024 * 1024) {
        if (source >= 0)
            close(source);
        lm_free(app);
        return lm_failure("shared file unavailable, unsafe or over 16 MiB");
    }
    char home[PATH_MAX - 64], path[PATH_MAX];
    const char *id = lm_string(lm_get(app, "id"), "");
    int size = snprintf(home, sizeof(home), "%s/data/%s", d->options.root, id);
    lm_free(app);
    if (size < 0 || size >= (int)sizeof(home) || lm_mkdir(home, 0700)) {
        close(source);
        return lm_failure("cannot prepare application home");
    }
    int homefd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (homefd < 0) {
        close(source);
        return lm_failure("unsafe application home");
    }
    if (fchown(homefd, (uid_t)target_uid, (gid_t)target_uid) || fchmod(homefd, 0700) ||
        (mkdirat(homefd, "imports", 0700) && errno != EEXIST)) {
        close(homefd);
        close(source);
        return lm_failure("cannot prepare import directory");
    }
    int directory = openat(homefd, "imports", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    close(homefd);
    if (directory < 0) {
        close(source);
        return lm_failure("unsafe import directory");
    }
    if (fchown(directory, (uid_t)target_uid, (gid_t)target_uid) || fchmod(directory, 0700)) {
        close(directory);
        close(source);
        return lm_failure("cannot own import directory");
    }
    unsigned long long token;
    char name[48];
    int output = -1;
    for (int i = 0; i < 8; ++i) {
        if (getrandom(&token, sizeof(token), 0) != (ssize_t)sizeof(token))
            break;
        snprintf(name, sizeof(name), "document-%016llx", token);
        output =
            openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (output >= 0 || errno != EEXIST)
            break;
    }
    if (output < 0) {
        close(directory);
        close(source);
        return lm_failure("cannot create imported document");
    }
    int error = 0;
    char buffer[8192];
    size_t copied = 0;
    ssize_t n;
    while ((n = read(source, buffer, sizeof(buffer))) != 0) {
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            error = 1;
            break;
        }
        copied += (size_t)n;
        if (copied > 16 * 1024 * 1024) {
            error = 1;
            break;
        }
        size_t offset = 0;
        while (offset < (size_t)n) {
            ssize_t k = write(output, buffer + offset, (size_t)n - offset);
            if (k < 0 && errno == EINTR)
                continue;
            if (k <= 0) {
                error = 1;
                break;
            }
            offset += (size_t)k;
        }
        if (error)
            break;
    }
    if (fchown(output, (uid_t)target_uid, (gid_t)target_uid) || fsync(output) || fsync(directory))
        error = 1;
    close(output);
    close(source);
    if (snprintf(path, sizeof(path), "%s/imports/%s", home, name) >= (int)sizeof(path))
        error = 1;
    if (error)
        unlinkat(directory, name, 0);
    close(directory);
    if (error)
        return lm_failure("file import failed");
    lm_json *out = lm_success();
    lm_set_string(out, "path", path);
    return out;
}
lm_json *portal_files(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    if (!lm_trusted(uid))
        return lm_failure("permission denied");
    const char *relative = lm_string(lm_get(r, "directory"), "");
    if (strlen(relative) > 240 || relative[0] == '/')
        return lm_failure("invalid directory");
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/shared", d->options.root) >= (int)sizeof(path))
        return lm_failure("path too long");
    int base = open(path, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct open_how how = {.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC,
                           .resolve =
                               RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS};
    int fd = base < 0
                 ? -1
                 : (int)syscall(SYS_openat2, base, *relative ? relative : ".", &how, sizeof(how));
    if (base >= 0)
        close(base);
    DIR *directory = fd < 0 ? NULL : fdopendir(fd);
    if (!directory) {
        if (fd >= 0)
            close(fd);
        return lm_failure("shared directory unavailable");
    }
    lm_json *out = lm_success(), *items = lm_array();
    struct dirent *entry;
    while (lm_size(items) < 128 && (entry = readdir(directory))) {
        if (entry->d_name[0] == '.' || strlen(entry->d_name) > 180)
            continue;
        struct stat info;
        if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) ||
            (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)))
            continue;
        lm_json *item = lm_object();
        lm_set_string(item, "name", entry->d_name);
        lm_set_boolean(item, "directory", S_ISDIR(info.st_mode));
        lm_set_integer(item, "bytes", info.st_size);
        lm_append(items, item);
    }
    closedir(directory);
    lm_set(out, "files", items);
    return out;
}
