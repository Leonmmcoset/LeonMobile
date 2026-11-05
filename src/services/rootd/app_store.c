#include "apps.h"
#include "auth.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int filename(const struct root_options *o, const char *name, char out[PATH_MAX]) {
    return snprintf(out, PATH_MAX, "%s/%s", o->directory, name) >= PATH_MAX ? -1 : 0;
}
int root_apps_lock(const struct root_options *o) {
    char path[PATH_MAX];
    if (filename(o, "apps.lock", path))
        return -1;
    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    struct stat s;
    if (fd < 0)
        return -1;
    if (fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_uid || (s.st_mode & 077) || s.st_nlink != 1) {
        close(fd);
        return -1;
    }
    int64_t end = lm_now_ms() + 200;
    while (flock(fd, LOCK_EX | LOCK_NB)) {
        if (errno != EWOULDBLOCK || lm_now_ms() >= end) {
            close(fd);
            return -1;
        }
        usleep(2000);
    }
    return fd;
}
lm_json *root_apps_read(const struct root_options *o, const char *name, int *missing) {
    *missing = 0;
    char path[PATH_MAX];
    if (filename(o, name, path))
        return NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        *missing = errno == ENOENT;
        return NULL;
    }
    struct stat s;
    char *text = malloc(65537);
    lm_json *result = NULL;
    if (!text || fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_uid || (s.st_mode & 077) ||
        s.st_nlink != 1 || s.st_size < 1 || s.st_size > 65536)
        goto done;
    size_t used = 0;
    while (used < (size_t)s.st_size) {
        ssize_t n = read(fd, text + used, (size_t)s.st_size - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            goto done;
        used += (size_t)n;
    }
    result = lm_parse(text, used);
done:
    free(text);
    close(fd);
    return result;
}
int root_apps_write(const struct root_options *o, const char *name, const lm_json *j) {
    char path[PATH_MAX];
    char *text = lm_dump(j);
    int result =
        !text || strlen(text) > 65536 || filename(o, name, path) ? -1 : lm_write_text(path, text);
    free(text);
    return result;
}
void root_apps_name(char *out, size_t size, pid_t worker) {
    snprintf(out, size, "request-%d.json", worker);
}
static int request_pid(const char *name) {
    int pid = 0;
    char tail;
    return sscanf(name, "request-%d.json%c", &pid, &tail) == 1 && pid > 1 ? pid : 0;
}
lm_json *root_apps_records(const struct root_options *o) {
    DIR *dir = opendir(o->directory);
    if (!dir)
        return NULL;
    lm_json *list = lm_array();
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        int pid = request_pid(entry->d_name), missing;
        if (!pid)
            continue;
        lm_json *j = root_apps_read(o, entry->d_name, &missing);
        if (!lm_is_object(j) || lm_integer(lm_get(j, "worker"), 0) != pid ||
            lm_size(list) >= ROOT_APP_REQUESTS) {
            lm_free(j);
            lm_free(list);
            list = NULL;
            break;
        }
        lm_append(list, j);
    }
    closedir(dir);
    return list;
}
int root_apps_audit(const struct root_options *o, const lm_json *record, const char *event,
                    int status) {
    int missing;
    lm_json *old = root_apps_read(o, "audit.json", &missing);
    if (!old && !missing)
        return -1;
    if (old && (!lm_is_array(old) || lm_size(old) > 64)) {
        lm_free(old);
        return -1;
    }
    lm_json *list = lm_array();
    for (size_t i = lm_size(old) > 63 ? lm_size(old) - 63 : 0; i < lm_size(old); ++i)
        lm_append(list, lm_copy(lm_at(old, i)));
    lm_free(old);
    lm_json *j = lm_object();
    for (const char **key = (const char *[]){"id", "version", "installation", "token", NULL}; *key;
         ++key)
        lm_set_string(j, *key, lm_string(lm_get(record, *key), ""));
    lm_set_integer(j, "uid", lm_integer(lm_get(record, "uid"), -1));
    lm_set_integer(j, "time", time(NULL));
    lm_set_string(j, "event", event);
    lm_set_integer(j, "status", status);
    lm_append(list, j);
    int result = root_apps_write(o, "audit.json", list);
    lm_free(list);
    return result;
}
int root_apps_cleanup(const struct root_options *o, pid_t worker) {
    int lock = root_apps_lock(o);
    if (lock < 0)
        return -1;
    DIR *dir = opendir(o->directory);
    if (!dir) {
        close(lock);
        return -1;
    }
    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        int pid = request_pid(entry->d_name), missing;
        if (!pid || (worker > 0 && worker != pid))
            continue;
        lm_json *j = root_apps_read(o, entry->d_name, &missing);
        if (worker == -1 && j && !kill(pid, 0) &&
            root_process_start(pid) == lm_integer(lm_get(j, "worker_start"), -1)) {
            lm_free(j);
            continue;
        }
        // No pending approval survives a daemon restart or an interrupted worker.
        if (j && root_apps_audit(o, j, "interrupted", -1))
            result = -1;
        lm_free(j);
        char path[PATH_MAX];
        if (filename(o, entry->d_name, path) || unlink(path))
            result = -1;
    }
    closedir(dir);
    close(lock);
    return result;
}
int64_t root_process_start(pid_t pid) {
    char path[64], text[4096];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    if (lm_read_text(path, text, sizeof(text)))
        return -1;
    char *end = strrchr(text, ')');
    if (!end || end[1] != ' ')
        return -1;
    char *part = end + 2, *next;
    for (int field = 3; field < 22; ++field) {
        next = strchr(part, ' ');
        if (!next)
            return -1;
        part = next + 1;
    }
    errno = 0;
    long long value = strtoll(part, &next, 10);
    return errno || next == part || value <= 0 ? -1 : value;
}
int root_apps_unlocked(const struct root_options *o, char epoch[33]) {
    char response[LEON_AUTH_LIMIT];
    struct auth_state state;
    if (auth_exchange(o->auth_socket, "{\"op\":\"status\"}\n", response, 250) ||
        auth_decode(response, &state) || !state.ok || state.locked || strlen(state.epoch) != 32)
        return 0;
    memcpy(epoch, state.epoch, 33);
    return 1;
}
int root_apps_current(const struct root_options *o, const lm_json *r) {
    pid_t pid = (pid_t)lm_integer(lm_get(r, "pid"), -1);
    if (root_process_start(pid) != lm_integer(lm_get(r, "start"), -2))
        return 0;
    lm_json *app =
        lm_root_app(o->app_root, o->builtin, (uid_t)lm_integer(lm_get(r, "uid"), -1), pid);
    int valid = app &&
                !strcmp(lm_string(lm_get(app, "installation"), ""),
                        lm_string(lm_get(r, "installation"), "!")) &&
                !strcmp(lm_string(lm_get(app, "helper"), ""), lm_string(lm_get(r, "helper"), "!"));
    lm_free(app);
    char epoch[33];
    return valid && root_apps_unlocked(o, epoch) &&
           !strcmp(epoch, lm_string(lm_get(r, "epoch"), "!"));
}
