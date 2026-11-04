#include "service.h"
#include "daemon.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

struct storage_state {
    long long next, generation;
    uint64_t fingerprint;
    char error[256];
};
static int device_name(const char *name) {
    if (!*name || strlen(name) > 63)
        return 0;
    for (const char *p = name; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')))
            return 0;
    return 1;
}
static int removable(struct lm_daemon *d, const char *name) {
    char path[PATH_MAX], text[32];
    snprintf(path, sizeof(path), "%s/class/block/%s/removable", d->options.sys, name);
    if (!lm_read_text(path, text, sizeof(text)) && !strcmp(text, "1\n"))
        return 1;
    snprintf(path, sizeof(path), "%s/class/block/%s/../removable", d->options.sys, name);
    return !lm_read_text(path, text, sizeof(text)) && !strcmp(text, "1\n");
}
static lm_json *status(struct lm_daemon *d) {
    struct storage_state *state = d->data;
    lm_json *out = lm_success(), *volumes = lm_array(), *devices = lm_array();
    const char *mounts[] = {"/", "/data", "/tmp"};
    for (size_t i = 0; i < 3; ++i) {
        struct statvfs info;
        lm_json *v = lm_object();
        lm_set_string(v, "path", mounts[i]);
        if (statvfs(d->options.test ? d->options.state : mounts[i], &info)) {
            lm_set_boolean(v, "available", 0);
            lm_set_string(v, "error", strerror(errno));
            lm_append(volumes, v);
            continue;
        }
        lm_set_boolean(v, "available", 1);
        lm_set_boolean(v, "low_space",
                       info.f_bavail < info.f_blocks / 20 ||
                           (long long)info.f_bavail * info.f_frsize < 64 * 1024 * 1024);
        lm_set_integer(v, "total_bytes", (long long)info.f_blocks * info.f_frsize);
        lm_set_integer(v, "available_bytes", (long long)info.f_bavail * info.f_frsize);
        lm_set_boolean(v, "read_only", info.f_flag & ST_RDONLY);
        lm_append(volumes, v);
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/class/block", d->options.sys);
    DIR *directory = opendir(path);
    if (directory) {
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (!device_name(entry->d_name))
                continue;
            lm_json *device = lm_object();
            lm_set_string(device, "name", entry->d_name);
            lm_set_boolean(device, "removable", removable(d, entry->d_name));
            char text[64] = "";
            snprintf(path, sizeof(path), "%s/class/block/%s/size", d->options.sys, entry->d_name);
            if (!lm_read_text(path, text, sizeof(text)))
                lm_set_integer(device, "bytes", strtoll(text, NULL, 10) * 512);
            snprintf(path, sizeof(path), "%s/fs/ext4/%s/errors_count", d->options.sys,
                     entry->d_name);
            if (!lm_read_text(path, text, sizeof(text)))
                lm_set_integer(device, "filesystem_errors", strtoll(text, NULL, 10));
            lm_append(devices, device);
        }
        closedir(directory);
    }
    lm_set(out, "filesystems", volumes);
    lm_set(out, "devices", devices);
    char text[32768] = "";
    snprintf(path, sizeof(path), "%s/self/mountinfo", d->options.proc);
    lm_read_text(path, text, sizeof(text));
    lm_set_string(out, "mountinfo", text);
    lm_set_string(out, "last_error", state->error);
    char *encoded = lm_dump(out);
    uint64_t hash = 1469598103934665603ULL;
    if (encoded)
        for (const unsigned char *p = (unsigned char *)encoded; *p; ++p) {
            hash ^= *p;
            hash *= 1099511628211ULL;
        }
    free(encoded);
    int changed = hash != state->fingerprint;
    if (changed) {
        state->fingerprint = hash;
        ++state->generation;
    }
    lm_set_integer(out, "generation", state->generation);
    if (changed && !d->options.test) {
        if (lm_write_json("/run/leon/storage-state.json", out) ||
            chmod("/run/leon/storage-state.json", 0644))
            fprintf(stderr, "storaged: cannot publish state\n");
    }
    return out;
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "status"))
        return status(d);
    if (!lm_trusted(uid))
        return lm_failure("permission denied");
    if (strcmp(op, "mount") && strcmp(op, "unmount"))
        return lm_failure("unknown operation");
    const char *name = lm_string(lm_get(r, "device"), "");
    if (!device_name(name))
        return lm_failure("invalid block-device name");
    if (!strncmp(name, "vda", 3) || !strcmp(name, "loop0") || !removable(d, name))
        return lm_failure("only removable media may be managed");
    if (d->options.test)
        return lm_failure("mount mutations disabled for test roots");
    char source[128], target[128];
    snprintf(source, sizeof(source), "/dev/%s", name);
    snprintf(target, sizeof(target), "/media/%s", name);
    struct stat info;
    if (lstat(source, &info) || !S_ISBLK(info.st_mode))
        return lm_failure("not a block device");
    int result;
    if (!strcmp(op, "mount")) {
        if (lm_mkdir(target, 0755))
            return lm_failure("cannot create mount point");
        result = mount(source, target, "ext4", MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_RDONLY, NULL);
    } else
        result = umount2(target, 0);
    struct storage_state *state = d->data;
    if (result) {
        snprintf(state->error, sizeof(state->error), "%s", strerror(errno));
        return lm_failure(state->error);
    }
    state->error[0] = 0;
    return status(d);
}
static void tick(struct lm_daemon *d) {
    struct storage_state *s = d->data;
    if (lm_now_ms() < s->next)
        return;
    s->next = lm_now_ms() + 2000;
    lm_json *snapshot = status(d);
    lm_free(snapshot);
}
int storaged_run(int argc, char **argv) {
    struct storage_state state = {0};
    struct lm_daemon d = {.options = {"/run/leon/storage.sock", "/data", "/data/leon",
                                      "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &state,
                          .tick = tick,
                          .handle = handle};
    if (lm_options(argc, argv, &d.options))
        return 2;
    return lm_serve(&d);
}
