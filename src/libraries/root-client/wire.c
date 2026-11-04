#include "wire.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
void root_clear(void *memory, size_t size) {
    volatile unsigned char *p = memory;
    while (size--)
        *p++ = 0;
}
int root_connect(const char *path) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct timeval timeout = {.tv_sec = 10};
    struct ucred peer;
    socklen_t size = sizeof(peer);
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid != 0) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    return fd;
}
int root_send(int fd, const char *text, int descriptor) {
    size_t size = strlen(text), sent = 0;
    while (sent < size) {
        struct iovec vector = {(void *)(text + sent), size - sent};
        union {
            struct cmsghdr align;
            char bytes[CMSG_SPACE(sizeof(int))];
        } control;
        struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1};
        if (!sent && descriptor >= 0) {
            memset(&control, 0, sizeof(control));
            message.msg_control = control.bytes;
            message.msg_controllen = sizeof(control);
            struct cmsghdr *header = CMSG_FIRSTHDR(&message);
            header->cmsg_level = SOL_SOCKET;
            header->cmsg_type = SCM_RIGHTS;
            header->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(header), &descriptor, sizeof(int));
        }
        ssize_t count = sendmsg(fd, &message, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        sent += (size_t)count;
    }
    return 0;
}
int root_receive(int fd, char *text, size_t capacity, int *descriptor) {
    size_t used = 0;
    int received = -1;
    while (used + 1 < capacity) {
        /* Reading one byte keeps a following session frame in the stream. */
        struct iovec vector = {text + used, 1};
        union {
            struct cmsghdr align;
            char bytes[CMSG_SPACE(sizeof(int) * 4)];
        } control;
        struct msghdr message = {.msg_iov = &vector,
                                 .msg_iovlen = 1,
                                 .msg_control = control.bytes,
                                 .msg_controllen = sizeof(control)};
        ssize_t count = recvmsg(fd, &message, MSG_CMSG_CLOEXEC);
        if (count < 0 && errno == EINTR)
            continue;
        int invalid = message.msg_flags & (MSG_CTRUNC | MSG_TRUNC);
        for (struct cmsghdr *h = CMSG_FIRSTHDR(&message); count > 0 && h;
             h = CMSG_NXTHDR(&message, h)) {
            if (h->cmsg_level != SOL_SOCKET || h->cmsg_type != SCM_RIGHTS ||
                h->cmsg_len < CMSG_LEN(sizeof(int))) {
                invalid = 1;
                continue;
            }
            size_t n = (h->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < n; ++i) {
                int value;
                memcpy(&value, (char *)CMSG_DATA(h) + i * sizeof(int), sizeof(value));
                if (received < 0 && descriptor)
                    received = value;
                else {
                    close(value);
                    invalid = 1;
                }
            }
        }
        if (count <= 0 || invalid || !text[used])
            break;
        if (text[used++] == '\n') {
            text[used] = 0;
            if (descriptor)
                *descriptor = received;
            return 0;
        }
    }
    if (received >= 0)
        close(received);
    root_clear(text, capacity);
    errno = EPROTO;
    return -1;
}
