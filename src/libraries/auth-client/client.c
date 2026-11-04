#include "auth.h"
#include "wire.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
static int64_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
/* A local connect must never stall the compositor if the backlog is full. */
static int connect_nonblocking(const char *path) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path))
        return -1;
    strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    struct ucred peer;
    socklen_t size = sizeof(peer);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid) {
        close(fd);
        return -1;
    }
    return fd;
}
static int wait_io(int fd, short events, int64_t deadline) {
    struct pollfd p = {fd, events, 0};
    for (;;) {
        int64_t remaining = deadline - now_ms();
        if (remaining <= 0)
            return -1;
        int result = poll(&p, 1, (int)remaining);
        if (result < 0 && errno == EINTR)
            continue;
        return result > 0 && (p.revents & events) ? 0 : -1;
    }
}
int auth_exchange(const char *path, const char *request, char *response, unsigned timeout_ms) {
    int fd = connect_nonblocking(path);
    if (fd < 0)
        return -1;
    int64_t deadline = now_ms() + (timeout_ms > 15000 ? 15000 : timeout_ms);
    size_t sent = 0, total = strlen(request), received = 0;
    int result = -1;
    while (sent < total) {
        if (wait_io(fd, POLLOUT, deadline))
            goto done;
        ssize_t n = send(fd, request + sent, total - sent, MSG_NOSIGNAL);
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (n <= 0)
            goto done;
        sent += (size_t)n;
    }
    while (received < LEON_AUTH_LIMIT - 1) {
        if (wait_io(fd, POLLIN, deadline))
            goto done;
        char controls[CMSG_SPACE(sizeof(int) * 8)];
        struct iovec v = {response + received, LEON_AUTH_LIMIT - 1 - received};
        struct msghdr m = {.msg_iov = &v,
                           .msg_iovlen = 1,
                           .msg_control = controls,
                           .msg_controllen = sizeof(controls)};
        ssize_t n = recvmsg(fd, &m, MSG_CMSG_CLOEXEC);
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        int invalid = m.msg_flags & (MSG_CTRUNC | MSG_TRUNC);
        for (struct cmsghdr *h = n > 0 ? CMSG_FIRSTHDR(&m) : NULL; h; h = CMSG_NXTHDR(&m, h)) {
            invalid = 1;
            if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_RIGHTS) {
                size_t count = (h->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                for (size_t i = 0; i < count; ++i) {
                    int extra;
                    memcpy(&extra, (char *)CMSG_DATA(h) + sizeof(int) * i, sizeof(extra));
                    close(extra);
                }
            }
        }
        if (n <= 0 || invalid || memchr(response + received, 0, (size_t)n))
            goto done;
        received += (size_t)n;
        char *end = memchr(response, '\n', received);
        if (end) {
            if ((size_t)(end - response) + 1 != received)
                goto done;
            response[received] = 0;
            result = 0;
            break;
        }
    }
done:
    close(fd);
    if (result)
        root_clear(response, LEON_AUTH_LIMIT);
    return result;
}
int auth_lock(const char *path) {
    char response[LEON_AUTH_LIMIT];
    struct auth_state state;
    return auth_exchange(path, "{\"op\":\"lock\"}\n", response, 300) ||
                   auth_decode(response, &state) || !state.ok || !state.locked
               ? -1
               : 0;
}
int auth_watch(const char *path) {
    int fd = connect_nonblocking(path);
    if (fd < 0)
        return -1;
    if (send(fd, "{\"op\":\"watch\"}\n", 15, MSG_DONTWAIT | MSG_NOSIGNAL) != 15) {
        close(fd);
        return -1;
    }
    return fd;
}
int auth_verify_peer(const char *path, pid_t pid, uid_t uid, const char *role) {
    if (strcmp(role, "shell") && strcmp(role, "ime"))
        return 0;
    char request[160], response[LEON_AUTH_LIMIT];
    struct auth_state state;
    snprintf(request, sizeof(request),
             "{\"op\":\"verify_peer\",\"pid\":%d,\"uid\":%u,\"role\":\"%s\"}\n", pid, uid, role);
    return !auth_exchange(path, request, response, 80) && !auth_decode(response, &state) &&
           state.ok && state.trusted;
}
