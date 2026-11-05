#include "capability.h"
#include "linux_openat2.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
int lm_open_beneath(const char *basepath, const char *relative, int flags) {
    if (!relative || !*relative || relative[0] == '/' || strlen(relative) > 240)
        return -1;
    int base = open(basepath, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct open_how how = {.flags = (unsigned)(flags | O_CLOEXEC),
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS};
    int fd = base < 0 ? -1 : (int)syscall(SYS_openat2, base, relative, &how, sizeof(how));
    if (base >= 0) close(base);
    return fd;
}
lm_json *lm_shared_files(struct lm_daemon *d, uid_t uid, const lm_json *r) {
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
