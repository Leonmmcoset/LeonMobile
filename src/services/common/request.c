#include "daemon.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static int wait_io(int fd, short events, long long deadline) {
    for (;;) {
        long long left = deadline - lm_now_ms();
        if (left <= 0)
            return -1;
        struct pollfd p = {fd, events, 0};
        int n = poll(&p, 1, (int)left);
        if (n < 0 && errno == EINTR)
            continue;
        return n > 0 && (p.revents & (events | POLLHUP)) ? 0 : -1;
    }
}
lm_json *lm_rpc_bounded(const char *path, const lm_json *request, unsigned timeout_ms) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path) || !timeout_ms || timeout_ms > 10000)
        return NULL;
    strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return NULL;
    long long deadline = lm_now_ms() + timeout_ms;
    char *text = lm_dump(request), *out = malloc(1024 * 1024);
    lm_json *result = NULL;
    if (!text || !out || strlen(text) > LM_REQUEST_LIMIT + 1)
        goto done;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (errno != EINPROGRESS || wait_io(fd, POLLOUT, deadline) ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) || error)
            goto done;
    }
    size_t left = strlen(text), sent = 0;
    while (sent < left) {
        ssize_t n = send(fd, text + sent, left - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_io(fd, POLLOUT, deadline))
                goto done;
            continue;
        }
        if (n <= 0)
            goto done;
        sent += (size_t)n;
    }
    shutdown(fd, SHUT_WR);
    size_t used = 0;
    for (;;) {
        if (used == 1024 * 1024 || lm_now_ms() >= deadline)
            goto done;
        ssize_t n = read(fd, out + used, 1024 * 1024 - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_io(fd, POLLIN, deadline))
                goto done;
            continue;
        }
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
lm_json *lm_rpc(const char *path, const lm_json *request) {
    return lm_rpc_bounded(path, request, 4000);
}
