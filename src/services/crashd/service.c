#include "service.h"
#include "daemon.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct crash_state {
    lm_json *exits;
};
#define CORE_LIMIT (8 * 1024 * 1024)
static int valid_report(const char *name) {
    if (strncmp(name, "report-", 7) || strlen(name) > 100)
        return 0;
    for (const char *p = name; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              *p == '-'))
            return 0;
    return 1;
}
static int erase(const char *root, const char *name) {
    if (!valid_report(name))
        return -1;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/core", root, name);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%s/report.json", root, name);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%s", root, name);
    return rmdir(path);
}
static void prune(const char *root) {
    struct dirent **entries = NULL;
    int count = scandir(root, &entries, NULL, alphasort), reports = 0;
    for (int i = 0; i < count; ++i)
        if (valid_report(entries[i]->d_name))
            ++reports;
    for (int i = 0; i < count; ++i) {
        if (reports > 8 && valid_report(entries[i]->d_name)) {
            erase(root, entries[i]->d_name);
            --reports;
        }
        free(entries[i]);
    }
    free(entries);
}
static int number(const char *text, long long *out) {
    if (!*text)
        return -1;
    errno = 0;
    char *end;
    *out = strtoll(text, &end, 10);
    return errno || *end || *out < 0 ? -1 : 0;
}
static int capture(int argc, char **argv) {
    if (argc != 6 && argc != 8)
        return 2;
    long long pid, uid, signal, timestamp;
    if (number(argv[2], &pid) || number(argv[3], &uid) || number(argv[4], &signal) ||
        number(argv[5], &timestamp) || pid < 1 || pid > INT_MAX || uid > 60000 || signal > 64)
        return 2;
    const char *root = "/data/leon/crashes";
    if (argc == 8) {
        if (strcmp(argv[6], "--state"))
            return 2;
        root = argv[7];
    }
    if (geteuid() || lm_mkdir(root, 0700) || chmod(root, 0700))
        return 1;
    char lockpath[PATH_MAX];
    if (snprintf(lockpath, sizeof(lockpath), "%s/.capture-lock", root) >= (int)sizeof(lockpath))
        return 1;
    int lock = open(lockpath, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0)
        return 1;
    long long wait_end = lm_now_ms() + 5000;
    while (flock(lock, LOCK_EX | LOCK_NB)) {
        if (errno != EWOULDBLOCK || lm_now_ms() >= wait_end) {
            close(lock);
            return 1;
        }
        usleep(10000);
    }
    char directory[PATH_MAX - 64], path[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/report-%020lld-%010lld-XXXXXX", root, timestamp,
             pid);
    if (!mkdtemp(directory))
        return 1;
    snprintf(path, sizeof(path), "%s/core", directory);
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return 1;
    size_t used = 0;
    int truncated = 0, error = 0;
    long long end = lm_now_ms() + 5000;
    int flags = fcntl(0, F_GETFL);
    if (flags >= 0)
        fcntl(0, F_SETFL, flags | O_NONBLOCK);
    while (lm_now_ms() < end) {
        struct pollfd ready = {0, POLLIN, 0};
        if (poll(&ready, 1, 100) < 0 && errno != EINTR) {
            error = 1;
            break;
        }
        char data[16384];
        ssize_t n = read(0, data, sizeof(data));
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n < 0) {
            error = 1;
            break;
        }
        if (!n)
            break;
        size_t write_size = (size_t)n;
        if (write_size > CORE_LIMIT - used) {
            write_size = CORE_LIMIT - used;
            truncated = 1;
        }
        size_t offset = 0;
        while (offset < write_size) {
            ssize_t written = write(fd, data + offset, write_size - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                error = 1;
                break;
            }
            offset += (size_t)written;
        }
        used += offset;
        if (error || truncated)
            break;
    }
    if (lm_now_ms() >= end)
        truncated = 1;
    if (fsync(fd))
        error = 1;
    close(fd);
    lm_json *report = lm_object();
    lm_set_integer(report, "pid", pid);
    lm_set_integer(report, "uid", uid);
    lm_set_integer(report, "signal", signal);
    lm_set_integer(report, "timestamp", timestamp);
    lm_set_integer(report, "core_bytes", used);
    lm_set_boolean(report, "truncated", truncated);
    lm_set_boolean(report, "io_error", error);
    lm_json *app = lm_app("/data/leon", "/usr/share/leon/apps", (uid_t)uid);
    lm_set_string(report, "app", lm_string(lm_get(app, "id"), ""));
    lm_free(app);
    snprintf(path, sizeof(path), "%s/report.json", directory);
    int result = lm_write_json(path, report);
    lm_free(report);
    prune(root);
    close(lock);
    return result ? 1 : 0;
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    const char *op = lm_string(lm_get(r, "op"), "");
    struct crash_state *state = d->data;
    if (!strcmp(op, "record_exit")) {
        if (uid)
            return lm_failure("permission denied");
        const char *id = lm_string(lm_get(r, "app"), "");
        lm_json *app = lm_app_by_id(d->options.root, d->options.builtin, id);
        long long status = lm_integer(lm_get(r, "status"), -1),
                  pid = lm_integer(lm_get(r, "pid"), -1);
        if (!app || status < 0 || status > 255 || pid < 1 || pid > INT_MAX) {
            lm_free(app);
            return lm_failure("invalid exit record");
        }
        lm_json *item = lm_object();
        lm_set_string(item, "app", id);
        lm_set_integer(item, "uid", lm_integer(lm_get(app, "uid"), -1));
        lm_set_integer(item, "pid", pid);
        lm_set_integer(item, "status", status);
        lm_set_integer(item, "timestamp", time(NULL));
        lm_free(app);
        lm_json *next = lm_array();
        size_t count = lm_size(state->exits);
        for (size_t i = count > 63 ? count - 63 : 0; i < count; ++i)
            lm_append(next, lm_copy(lm_at(state->exits, i)));
        lm_append(next, item);
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/exits.json", d->options.state) >= (int)sizeof(path) ||
            lm_write_json(path, next)) {
            lm_free(next);
            return lm_failure("cannot persist exit history");
        }
        lm_free(state->exits);
        state->exits = next;
        return lm_success();
    }
    if (!strcmp(op, "remove")) {
        if (!lm_trusted(uid))
            return lm_failure("permission denied");
        if (erase(d->options.state, lm_string(lm_get(r, "id"), "")))
            return lm_failure("cannot remove crash report");
        return lm_success();
    }
    if (strcmp(op, "list"))
        return lm_failure("unknown operation");
    lm_json *out = lm_success(), *items = lm_array();
    struct dirent **entries = NULL;
    int count = scandir(d->options.state, &entries, NULL, alphasort);
    for (int i = 0; i < count; ++i) {
        if (valid_report(entries[i]->d_name)) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s/report.json", d->options.state, entries[i]->d_name);
            lm_json *item = lm_read_json(path);
            if (lm_is_object(item) &&
                (lm_trusted(uid) || lm_integer(lm_get(item, "uid"), -1) == uid)) {
                lm_set_string(item, "id", entries[i]->d_name);
                lm_append(items, item);
            } else
                lm_free(item);
        }
        free(entries[i]);
    }
    free(entries);
    lm_set(out, "reports", items);
    lm_json *exits = lm_array();
    for (size_t i = 0; i < lm_size(state->exits); ++i) {
        const lm_json *item = lm_at(state->exits, i);
        if (lm_trusted(uid) || lm_integer(lm_get(item, "uid"), -1) == uid)
            lm_append(exits, lm_copy(item));
    }
    lm_set(out, "exits", exits);
    return out;
}
static int configure(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int rc = write(fd, value, strlen(value)) == (ssize_t)strlen(value) ? 0 : -1;
    close(fd);
    return rc;
}
int crashd_run(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--capture"))
        return capture(argc, argv);
    struct crash_state state = {0};
    struct lm_daemon d = {.options = {"/run/leon/crash.sock", "/data/leon/crashes", "/data/leon",
                                      "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &state,
                          .handle = handle};
    if (lm_options(argc, argv, &d.options) || lm_mkdir(d.options.state, 0700))
        return 2;
    if (!d.options.test) {
        if (configure("/proc/sys/kernel/core_pattern", "|/usr/sbin/crashd --capture %P %u %s %t") ||
            configure("/proc/sys/kernel/core_pipe_limit", "4"))
            return 1;
    }
    prune(d.options.state);
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/exits.json", d.options.state) >= (int)sizeof(path))
        return 2;
    state.exits = lm_read_json(path);
    if (!state.exits)
        state.exits = lm_array();
    if (!lm_at(state.exits, 0) && lm_size(state.exits) > 0) {
        lm_free(state.exits);
        state.exits = lm_array();
    }
    int result = lm_serve(&d);
    lm_free(state.exits);
    return result;
}
