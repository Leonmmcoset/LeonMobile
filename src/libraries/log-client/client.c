#include "log.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
_Static_assert(sizeof(struct leon_log_packet) == 1144, "log datagram ABI");
static int64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int address(const char *path, struct sockaddr_un *a) {
    memset(a, 0, sizeof(*a));
    a->sun_family = AF_UNIX;
    if (!path || strlen(path) >= sizeof(a->sun_path))
        return -1;
    strcpy(a->sun_path, path);
    return 0;
}
int leon_log_send(const char *path, const struct leon_log_packet *p) {
    struct sockaddr_un a;
    if (address(path, &a))
        return -1;
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    int result = sendto(fd, p, sizeof(*p), MSG_DONTWAIT | MSG_NOSIGNAL,
                        (struct sockaddr *)&a, sizeof(a)) == (ssize_t)sizeof(*p) ? 0 : -1;
    close(fd);
    return result;
}
int leon_log_write(const char *path, unsigned level, const char *tag, const void *data, size_t size) {
    if (level > 7 || !tag || strlen(tag) >= LEON_LOG_SOURCE || size > LEON_LOG_MESSAGE)
        return -1;
    struct leon_log_packet p = {.magic = LEON_LOG_MAGIC, .level = level, .length = (uint32_t)size};
    strcpy(p.source, tag);
    if (size)
        memcpy(p.message, data, size);
    return leon_log_send(path, &p);
}
static int wait_io(int fd, short events, int64_t deadline) {
    struct pollfd p = {fd, events, 0};
    for (;;) {
        int64_t remaining = deadline - now_ms();
        if (remaining <= 0)
            return -1;
        int n = poll(&p, 1, (int)remaining);
        if (n < 0 && errno == EINTR)
            continue;
        return n > 0 && (p.revents & events) ? 0 : -1;
    }
}
int leon_log_exchange(const char *path, const char *request, char *out, size_t cap, unsigned timeout) {
    struct sockaddr_un a;
    if (!cap || cap > LEON_LOG_RESPONSE || address(path, &a))
        return -1;
    out[0] = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    struct ucred peer;
    socklen_t size = sizeof(peer);
    int result = -1;
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid)
        goto done;
    int64_t deadline = now_ms() + (timeout > 5000 ? 5000 : timeout);
    size_t sent = 0, total = strlen(request), used = 0;
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
    while (used < cap - 1) {
        if (wait_io(fd, POLLIN, deadline))
            goto done;
        char controls[CMSG_SPACE(sizeof(int) * 16)];
        struct iovec v = {out + used, cap - 1 - used};
        struct msghdr m = {.msg_iov = &v, .msg_iovlen = 1,
                           .msg_control = controls, .msg_controllen = sizeof(controls)};
        ssize_t n = recvmsg(fd, &m, MSG_CMSG_CLOEXEC);
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        int invalid = m.msg_flags & (MSG_CTRUNC | MSG_TRUNC);
        for (struct cmsghdr *h = n > 0 ? CMSG_FIRSTHDR(&m) : NULL; h; h = CMSG_NXTHDR(&m, h)) {
            invalid = 1;
            if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_RIGHTS)
                for (size_t i = 0; i < (h->cmsg_len - CMSG_LEN(0)) / sizeof(int); ++i) {
                    int extra;
                    memcpy(&extra, (char *)CMSG_DATA(h) + i * sizeof(int), sizeof(extra));
                    close(extra);
                }
        }
        if (n <= 0 || invalid || memchr(out + used, 0, (size_t)n))
            goto done;
        used += (size_t)n;
        char *end = memchr(out, '\n', used);
        if (end) {
            if (end != out + used - 1)
                goto done;
            out[used] = 0;
            result = 0;
            break;
        }
    }
done:
    close(fd);
    if (result)
        memset(out, 0, cap);
    return result;
}
