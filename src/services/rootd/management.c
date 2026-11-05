#include "../common/linux_openat2.h"
#include "../serviced/config.h"
#include "../serviced/policy.h"
#include "rootd.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>
#define SERVICE_POLICY "/data/leon/developer/services"
static int relative_path(const char *path, int empty) {
    if ((!*path && !empty) || *path == '/' || strlen(path) > 240)
        return 0;
    const char *start = path;
    for (const char *p = path;; ++p) {
        if (!*p || *p == '/') {
            size_t size = (size_t)(p - start);
            if ((!size && *path) || (size == 1 && *start == '.') ||
                (size == 2 && start[0] == '.' && start[1] == '.'))
                return 0;
            if (!*p)
                return 1;
            start = p + 1;
        } else if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.')
            return 0;
    }
}
static int beneath(int directory, const char *path, int flags) {
    struct open_how how = {.flags = (unsigned long long)(flags | O_CLOEXEC | O_NOFOLLOW),
                           .resolve =
                               RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS};
    return (int)syscall(SYS_openat2, directory, *path ? path : ".", &how, sizeof(how));
}
static int read_fd(int fd, char *text, size_t size) {
    ssize_t count;
    do {
        count = read(fd, text, size - 1);
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        return -1;
    text[count] = 0;
    while (count && (text[count - 1] == '\n' || text[count - 1] == '\r'))
        text[--count] = 0;
    return 0;
}
static lm_json *kernel(const struct root_options *o, const lm_json *r, int authorized) {
    const char *path = lm_string(lm_get(r, "path"), "");
    char base[PATH_MAX];
    snprintf(base, sizeof(base), "%s/sys", o->proc);
    int directory = open(base, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0 || !relative_path(path, 1)) {
        if (directory >= 0)
            close(directory);
        return lm_failure("invalid or unavailable kernel parameter path");
    }
    int modifying = strcmp(lm_string(lm_get(r, "op"), ""), "inspect");
    if (modifying) {
        const char *value = lm_string(lm_get(r, "value"), "");
        if (!authorized || !*path || !*value || strlen(value) > 256) {
            close(directory);
            return lm_failure("invalid kernel value or missing root authentication");
        }
        for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
            if (*p < 32 || *p == 127) {
                close(directory);
                return lm_failure("kernel values must fit on one line");
            }
        int fd = beneath(directory, path, O_WRONLY | O_TRUNC);
        char text[258];
        int length = snprintf(text, sizeof(text), "%s\n", value);
        int result = fd < 0 || write(fd, text, (size_t)length) != length;
        if (fd >= 0)
            close(fd);
        close(directory);
        return result ? lm_failure("kernel rejected the value or parameter is read-only")
                      : lm_success();
    }
    int fd = beneath(directory, path, O_RDONLY | O_NONBLOCK);
    close(directory);
    struct stat info;
    if (fd < 0 || fstat(fd, &info)) {
        if (fd >= 0)
            close(fd);
        return lm_failure("kernel parameter unavailable");
    }
    lm_json *out = lm_success();
    if (!S_ISDIR(info.st_mode)) {
        char value[1024];
        if (read_fd(fd, value, sizeof(value))) {
            close(fd);
            lm_free(out);
            return lm_failure("kernel parameter is unreadable");
        }
        lm_set_string(out, "value", value);
        lm_set_boolean(out, "writable", (info.st_mode & S_IWUSR) != 0);
    } else {
        DIR *dir = fdopendir(fd);
        if (!dir) {
            close(fd);
            lm_free(out);
            return lm_failure("kernel directory is unreadable");
        }
        fd = -1;
        lm_json *items = lm_array();
        long long offset = lm_integer(lm_get(r, "offset"), 0);
        struct dirent *entry;
        int seen = 0;
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] == '.' || !relative_path(entry->d_name, 0))
                continue;
            if (seen++ < offset)
                continue;
            if (lm_size(items) == 12) {
                lm_set_boolean(out, "more", 1);
                break;
            }
            if (fstatat(dirfd(dir), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) ||
                S_ISLNK(info.st_mode))
                continue;
            char key[512];
            snprintf(key, sizeof(key), "%s%s%s", path, *path ? "/" : "", entry->d_name);
            lm_json *item = lm_object();
            lm_set_string(item, "name", entry->d_name);
            lm_set_string(item, "path", key);
            lm_set_boolean(item, "directory", S_ISDIR(info.st_mode));
            lm_append(items, item);
        }
        closedir(dir);
        lm_set(out, "items", items);
    }
    if (fd >= 0)
        close(fd);
    return out;
}
static int service_exchange(const struct root_options *o, const char *verb, const char *name,
                            char *out, size_t capacity) {
    int fd = root_connect(o->service_socket);
    if (fd < 0)
        return -1;
    char request[128];
    snprintf(request, sizeof(request), "%s %s\n", verb, name);
    int result = root_send(fd, request, -1);
    shutdown(fd, SHUT_WR);
    size_t used = 0;
    while (!result && used + 1 < capacity) {
        ssize_t count = read(fd, out + used, capacity - used - 1);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            result = -1;
            break;
        }
        if (!count)
            break;
        used += (size_t)count;
    }
    out[used] = 0;
    close(fd);
    return result;
}
static int service_find(const struct root_options *o, const char *name, struct service *service) {
    DIR *directory = opendir(o->services);
    if (!directory)
        return -1;
    struct dirent *entry;
    int result = -1;
    while ((entry = readdir(directory))) {
        size_t size = strlen(entry->d_name);
        if (size < 6 || strcmp(entry->d_name + size - 5, ".toml"))
            continue;
        char file[PATH_MAX], error[256];
        snprintf(file, sizeof(file), "%s/%s", o->services, entry->d_name);
        struct service candidate;
        if (service_load(file, &candidate, error, sizeof(error)))
            continue;
        if (!strcmp(candidate.name, name)) {
            *service = candidate;
            result = 0;
            break;
        }
        service_free(&candidate);
    }
    closedir(directory);
    return result;
}
static const char *restart_name(int mode) {
    return mode == 2 ? "always" : mode == 1 ? "on-failure" : "never";
}
static lm_json *services(const struct root_options *o, const lm_json *r, int authorized) {
    const char *op = lm_string(lm_get(r, "op"), ""), *name = lm_string(lm_get(r, "name"), "");
    char text[8192], policy_dir[PATH_MAX];
    if (o->test)
        snprintf(policy_dir, sizeof(policy_dir), "%s/services", o->directory);
    else
        snprintf(policy_dir, sizeof(policy_dir), "%s", SERVICE_POLICY);
    if (!strcmp(op, "inspect") && !*name) {
        if (service_exchange(o, "status", "", text, sizeof(text)))
            return lm_failure("service manager unavailable");
        lm_json *out = lm_success(), *items = lm_array();
        char *save = NULL;
        long long offset = lm_integer(lm_get(r, "offset"), 0);
        int seen = 0;
        for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (seen++ < offset)
                continue;
            if (lm_size(items) == 12) {
                lm_set_boolean(out, "more", 1);
                break;
            }
            char *tab = strchr(line, '\t');
            if (!tab)
                continue;
            *tab = 0;
            lm_json *item = lm_object();
            lm_set_string(item, "name", line);
            lm_set_string(item, "status", tab + 1);
            lm_append(items, item);
        }
        lm_set(out, "items", items);
        return out;
    }
    if (!*name || strlen(name) > 63 || !relative_path(name, 0) || strchr(name, '/') ||
        strchr(name, '.'))
        return lm_failure("invalid service name");
    struct service service;
    if (service_find(o, name, &service))
        return lm_failure("unknown service");
    if (service_policy_apply(policy_dir, &service)) {
        service_free(&service);
        return lm_failure("stored service configuration is invalid");
    }
    lm_json *out = NULL;
    if (!strcmp(op, "inspect")) {
        out = lm_success();
        lm_set_string(out, "name", name);
        lm_set_boolean(out, "autostart", service.autostart);
        lm_set_string(out, "restart", restart_name(service.restart));
        lm_set_integer(out, "restart_limit", service.restart_limit);
        lm_set_integer(out, "watchdog_ms", service.watchdog_ms);
        lm_set_integer(out, "uid", service.uid);
        lm_set_integer(out, "gid", service.gid);
        lm_set_string(out, "executable", service.argv[0]);
        if (!service_exchange(o, "status", name, text, sizeof(text)))
            lm_set_string(out, "status", text);
    } else if (!authorized)
        out = lm_failure("root authentication required");
    else if (!strcmp(name, "root"))
        out = lm_failure("manage the root broker itself from an authenticated root shell");
    else if (!strcmp(op, "service_config")) {
        int autostart = lm_boolean(lm_get(r, "autostart"), -1);
        const char *restart = lm_string(lm_get(r, "restart"), "");
        long long limit = lm_integer(lm_get(r, "restart_limit"), -1);
        if (autostart < 0 || limit < 0 || limit > 60000 ||
            (strcmp(restart, "never") && strcmp(restart, "always") &&
             strcmp(restart, "on-failure")))
            out = lm_failure("invalid boot/restart policy");
        else {
            char file[PATH_MAX];
            lm_json *json = lm_object();
            lm_set_boolean(json, "autostart", autostart);
            lm_set_string(json, "restart", restart);
            lm_set_integer(json, "restart_limit", limit);
            int saved =
                lm_mkdir(policy_dir, 0700) ||
                snprintf(file, sizeof(file), "%s/%s.json", policy_dir, name) >= (int)sizeof(file) ||
                lm_write_json(file, json);
            lm_free(json);
            out =
                saved ? lm_failure("cannot save service policy")
                : service_exchange(o, "reload", name, text, sizeof(text)) || strncmp(text, "OK", 2)
                    ? lm_failure("policy saved but service manager reload failed")
                    : lm_success();
        }
    } else if (!strcmp(op, "service_control")) {
        const char *action = lm_string(lm_get(r, "action"), "");
        if (strcmp(action, "start") && strcmp(action, "stop") && strcmp(action, "restart"))
            out = lm_failure("invalid service control action");
        else
            out = service_exchange(o, action, name, text, sizeof(text)) || strncmp(text, "OK", 2)
                      ? lm_failure("service manager rejected control request")
                      : lm_success();
    } else
        out = lm_failure("unknown service operation");
    service_free(&service);
    return out;
}
static int module_name(const char *name) {
    if (!*name || strlen(name) > 128 || *name == '-')
        return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (!isalnum(*p) && *p != '_' && *p != '-')
            return 0;
    return 1;
}
static lm_json *modules(const struct root_options *o, const lm_json *r, int authorized) {
    if (strcmp(lm_string(lm_get(r, "op"), ""), "inspect")) {
        const char *name = lm_string(lm_get(r, "name"), ""),
                   *action = lm_string(lm_get(r, "action"), "");
        if (!authorized || !module_name(name) ||
            (strcmp(action, "load") && strcmp(action, "unload")))
            return lm_failure("invalid module operation or missing authentication");
        if (o->test)
            return lm_failure("module changes are disabled in host fixtures");
        char *load[] = {"/sbin/modprobe", (char *)name, NULL};
        char *unload[] = {"/sbin/modprobe", "-r", (char *)name, NULL};
        return lm_child(!strcmp(action, "load") ? load : unload, NULL, 5000)
                   ? lm_failure("module operation failed; module may be unavailable or in use")
                   : lm_success();
    }
    char path[PATH_MAX], line[1024];
    int available = lm_boolean(lm_get(r, "available"), 0);
    struct utsname info;
    uname(&info);
    if (available)
        snprintf(path, sizeof(path), "%s/%s/modules.dep", o->modules, info.release);
    else
        snprintf(path, sizeof(path), "%s/modules", o->proc);
    FILE *file = fopen(path, "re");
    if (!file)
        return lm_failure(available ? "module catalog unavailable"
                                    : "loaded module list unavailable");
    lm_json *out = lm_success(), *items = lm_array();
    long long offset = lm_integer(lm_get(r, "offset"), 0);
    int seen = 0;
    while (fgets(line, sizeof(line), file)) {
        if (seen++ < offset)
            continue;
        if (lm_size(items) == 12) {
            lm_set_boolean(out, "more", 1);
            break;
        }
        char *end = strpbrk(line, available ? ":" : " \t");
        if (!end)
            continue;
        *end++ = 0;
        char *name = available ? strrchr(line, '/') : NULL;
        name = name ? name + 1 : line;
        char *suffix = available ? strstr(name, ".ko") : NULL;
        if (suffix)
            *suffix = 0;
        if (!module_name(name))
            continue;
        lm_json *item = lm_object();
        lm_set_string(item, "name", name);
        if (!available)
            lm_set_string(item, "status", end);
        lm_append(items, item);
    }
    fclose(file);
    lm_set(out, "items", items);
    return out;
}
static lm_json *core(const struct root_options *o) {
    struct utsname info;
    if (uname(&info))
        return lm_failure("kernel information unavailable");
    lm_json *out = lm_success(), *items = lm_array();
    lm_set_string(out, "kernel", info.release);
    lm_set_string(out, "architecture", info.machine);
    char path[PATH_MAX], text[2048];
    snprintf(path, sizeof(path), "%s/cmdline", o->proc);
    if (!lm_read_text(path, text, sizeof(text)))
        lm_set_string(out, "command_line", text);
    const char *components[][2] = {{"init", "/usr/sbin/leon-init"},
                                   {"leond", "/usr/sbin/leond"},
                                   {"rootd", "/usr/sbin/rootd"},
                                   {"authd", "/usr/sbin/authd"},
                                   {"application broker", "/usr/sbin/leon-launchd"},
                                   {"LMUI", "/usr/lib/liblmui.so.1"},
                                   {"musl", "/lib/ld-musl-aarch64.so.1"},
                                   {"compositor", "/usr/bin/leon-compositor"},
                                   {"mobile shell", "/usr/bin/leon-shell"}};
    for (size_t i = 0; i < sizeof(components) / sizeof(components[0]); ++i) {
        struct stat status;
        int present = !stat(components[i][1], &status);
        lm_json *item = lm_object();
        lm_set_string(item, "name", components[i][0]);
        lm_set_string(item, "path", components[i][1]);
        lm_set_boolean(item, "present", present);
        if (present)
            lm_set_integer(item, "bytes", status.st_size);
        lm_append(items, item);
    }
    lm_set(out, "items", items);
    return out;
}
lm_json *root_management(const struct root_options *o, const lm_json *r, int authorized) {
    const char *area = lm_string(lm_get(r, "area"), ""), *op = lm_string(lm_get(r, "op"), "");
    long long offset = lm_integer(lm_get(r, "offset"), 0);
    if (offset < 0 || offset > 65536)
        return lm_failure("invalid list offset");
    if (!strcmp(area, "kernel") && (!strcmp(op, "inspect") || !strcmp(op, "sysctl")))
        return kernel(o, r, authorized);
    if (!strcmp(area, "services") &&
        (!strcmp(op, "inspect") || !strcmp(op, "service_control") || !strcmp(op, "service_config")))
        return services(o, r, authorized);
    if (!strcmp(area, "modules") && (!strcmp(op, "inspect") || !strcmp(op, "module")))
        return modules(o, r, authorized);
    if (!strcmp(area, "core") && !strcmp(op, "inspect"))
        return core(o);
    return lm_failure("unknown developer operation");
}
