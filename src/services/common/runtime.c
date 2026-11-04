#include "daemon.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

int lm_options(int argc, char **argv, struct lm_options *o) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--test")) {
            o->test = 1;
            continue;
        }
        if (i + 1 == argc)
            return -1;
        const char **field = !strcmp(argv[i], "--socket")    ? &o->socket_path
                             : !strcmp(argv[i], "--state")   ? &o->state
                             : !strcmp(argv[i], "--root")    ? &o->root
                             : !strcmp(argv[i], "--builtin") ? &o->builtin
                             : !strcmp(argv[i], "--proc")    ? &o->proc
                             : !strcmp(argv[i], "--sys")     ? &o->sys
                                                             : NULL;
        if (!field)
            return -1;
        *field = argv[++i];
    }
    if (geteuid()) {
        fputs("system service requires root\n", stderr);
        return -1;
    }
    return 0;
}
int lm_trusted(uid_t uid) { return uid == 0 || uid == 1000; }
lm_json *lm_success(void) {
    lm_json *j = lm_object();
    lm_set_boolean(j, "ok", 1);
    return j;
}
lm_json *lm_failure(const char *message) {
    lm_json *j = lm_object();
    lm_set_boolean(j, "ok", 0);
    lm_set_string(j, "error", message);
    return j;
}
char *lm_error(const char *message) {
    lm_json *j = lm_failure(message);
    char *s = lm_dump(j);
    lm_free(j);
    return s;
}
char *lm_dispatch(struct lm_daemon *daemon, uid_t uid, const char *text, size_t size) {
    lm_json *request = lm_parse(text, size);
    if (!lm_is_object(request)) {
        lm_free(request);
        return lm_error("invalid JSON object");
    }
    lm_json *reply = daemon->handle(daemon, uid, request);
    lm_free(request);
    if (!reply)
        return lm_error("service operation failed");
    char *out = lm_dump(reply);
    lm_free(reply);
    return out;
}
int lm_mkdir(const char *path, mode_t mode) {
    char copy[PATH_MAX];
    if (!path || strlen(path) >= sizeof(copy))
        return -1;
    strcpy(copy, path);
    for (char *p = copy + 1; *p; ++p)
        if (*p == '/') {
            *p = 0;
            if (mkdir(copy, 0755) && errno != EEXIST)
                return -1;
            struct stat part;
            if (lstat(copy, &part) || !S_ISDIR(part.st_mode))
                return -1;
            *p = '/';
        }
    if (mkdir(copy, mode) && errno != EEXIST)
        return -1;
    struct stat info;
    return lstat(copy, &info) || !S_ISDIR(info.st_mode) ? -1 : 0;
}
int lm_read_text(const char *path, char *out, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return -1;
    size_t used = 0;
    while (used < size - 1) {
        ssize_t n = read(fd, out + used, size - 1 - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (!n)
            break;
        used += (size_t)n;
    }
    char extra;
    int excess = read(fd, &extra, 1) > 0;
    close(fd);
    out[used] = 0;
    return excess ? -1 : 0;
}
lm_json *lm_read_json(const char *path) {
    char *text = malloc(1024 * 1024 + 1);
    if (!text)
        return NULL;
    lm_json *j = !lm_read_text(path, text, 1024 * 1024 + 1) ? lm_parse(text, strlen(text)) : NULL;
    free(text);
    return j;
}
int lm_write_text(const char *path, const char *text) {
    char next[PATH_MAX], parent[PATH_MAX];
    if (snprintf(next, sizeof(next), "%s.XXXXXX", path) >= (int)sizeof(next) ||
        strlen(path) >= sizeof(parent))
        return -1;
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return -1;
    *slash = 0;
    if (lm_mkdir(parent, 0755))
        return -1;
    int fd = mkostemp(next, O_CLOEXEC);
    if (fd < 0)
        return -1;
    size_t left = strlen(text);
    while (left) {
        ssize_t n = write(fd, text, left);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            unlink(next);
            return -1;
        }
        left -= (size_t)n;
        text += n;
    }
    int result = fsync(fd);
    close(fd);
    if (result || rename(next, path)) {
        unlink(next);
        return -1;
    }
    fd = open(parent, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (fd < 0)
        return -1;
    result = fsync(fd);
    close(fd);
    return result;
}
int lm_write_json(const char *path, const lm_json *j) {
    char *text = lm_dump(j);
    if (!text)
        return -1;
    int result = lm_write_text(path, text);
    free(text);
    return result;
}
int lm_child_setup(pid_t parent) {
    sigset_t empty;
    sigemptyset(&empty);
    if (sigprocmask(SIG_SETMASK, &empty, NULL) || prctl(PR_SET_PDEATHSIG, SIGKILL) ||
        getppid() != parent)
        return -1;
    return 0;
}
int lm_child(char *const argv[], const char *input, unsigned timeout_ms) {
    (void)input;
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (!pid) {
        if (lm_child_setup(parent))
            _exit(126);
        setsid();
        int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            dup2(fd, 0);
            if (fd > 2)
                close(fd);
        }
        execv(argv[0], argv);
        _exit(127);
    }
    long long end = lm_now_ms() + timeout_ms;
    int status;
    while (lm_now_ms() < end) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        usleep(10000);
    }
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return -1;
}
lm_json *lm_rpc(const char *path, const lm_json *request) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path))
        return NULL;
    strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return NULL;
    struct timeval timeout = {.tv_sec = 4};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char *text = lm_dump(request), *out = malloc(1024 * 1024);
    lm_json *result = NULL;
    if (!text || !out || connect(fd, (struct sockaddr *)&address, sizeof(address)))
        goto done;
    size_t left = strlen(text), sent = 0;
    while (sent < left) {
        ssize_t n = send(fd, text + sent, left - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            goto done;
        sent += (size_t)n;
    }
    shutdown(fd, SHUT_WR);
    size_t used = 0;
    for (;;) {
        if (used == 1024 * 1024)
            goto done;
        ssize_t n = read(fd, out + used, 1024 * 1024 - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            goto done;
        if (!n)
            break;
        used += (size_t)n;
    }
    result = lm_parse(out, used);
done:
    free(text);
    free(out);
    close(fd);
    return result;
}
