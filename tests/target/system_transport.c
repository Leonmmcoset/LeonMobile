#define _GNU_SOURCE
#include "system_probe.h"
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
static char response[262144];
static int call(const char *service, const char *request) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (!strcmp(service, "root"))
        snprintf(address.sun_path, sizeof(address.sun_path), "/run/leon-root/control.sock");
    else if (!strcmp(service, "auth"))
        snprintf(address.sun_path, sizeof(address.sun_path), "/run/leon-auth/control.sock");
    else if (!strcmp(service, "log"))
        snprintf(address.sun_path, sizeof(address.sun_path), "/run/leon-log/control.sock");
    else
        snprintf(address.sun_path, sizeof(address.sun_path), "/run/leon/%s.sock", service);
    struct timeval timeout = {.tv_sec = 6};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        close(fd);
        return -1;
    }
    size_t sent = 0, size = strlen(request);
    while (sent < size) {
        ssize_t n = send(fd, request + sent, size - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        sent += (size_t)n;
    }
    if (send(fd, "\n", 1, MSG_NOSIGNAL) != 1) {
        close(fd);
        return -1;
    }
    shutdown(fd, SHUT_WR);
    size_t used = 0;
    while (used < sizeof(response) - 1) {
        ssize_t n = read(fd, response + used, sizeof(response) - 1 - used);
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
    close(fd);
    response[used] = 0;
    return used ? 0 : -1;
}
int probe_rpc(const char *service, const char *request, uid_t uid) {
    response[0] = 0;
    if (!uid)
        return call(service, request);
    int fds[2];
    if (pipe(fds))
        return -1;
    pid_t pid = fork();
    if (!pid) {
        close(fds[0]);
        if (setgroups(0, NULL) || setgid(uid) || setuid(uid) || call(service, request))
            _exit(1);
        size_t sent = 0, size = strlen(response);
        while (sent < size) {
            ssize_t n = write(fds[1], response + sent, size - sent);
            if (n <= 0)
                _exit(2);
            sent += (size_t)n;
        }
        _exit(0);
    }
    close(fds[1]);
    size_t used = 0;
    while (used < sizeof(response) - 1) {
        ssize_t n = read(fds[0], response + used, sizeof(response) - 1 - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        used += (size_t)n;
    }
    response[used] = 0;
    close(fds[0]);
    int status;
    if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status))
        return -1;
    return 0;
}
const char *probe_reply(void) { return response; }
int probe_has(const char *text) { return strstr(response, text) != NULL; }
long long probe_integer(const char *name) {
    char key[96];
    snprintf(key, sizeof(key), "\"%s\":", name);
    char *value = strstr(response, key);
    return value ? strtoll(value + strlen(key), NULL, 10) : -1;
}
