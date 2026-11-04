#include "../serviced/notify.h"
#include "authd.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
void auth_close(struct auth_daemon *d, int i) {
    if (d->watch == i) {
        d->watch = -1;
        auth_set_locked(d);
    }
    if (d->clients[i].fd >= 0)
        close(d->clients[i].fd);
    credential_clear(&d->clients[i], sizeof(d->clients[i]));
    d->clients[i].fd = -1;
}
void auth_reply(struct auth_daemon *d, int i, lm_json *reply) {
    char *text = lm_dump(reply);
    lm_free(reply);
    struct auth_client *c = &d->clients[i];
    if (!text || strlen(text) >= sizeof(c->reply) || c->size) {
        free(text);
        auth_close(d, i);
        return;
    }
    strcpy(c->reply, text);
    c->size = strlen(text);
    c->sent = 0;
    c->deadline = auth_now() + 1000;
    credential_clear(text, strlen(text));
    free(text);
}
static int peer_role(struct auth_daemon *d, struct ucred p) {
    if (!p.uid)
        return AUTH_ROOT;
    if (d->test)
        return p.uid == 11000 ? AUTH_SETTINGS : p.uid == 1000 ? AUTH_SHELL : AUTH_NONE;
    if (auth_peer(p.pid, p.uid, 1000, "/usr/bin/leon-shell", "/usr/sbin/leond"))
        return AUTH_SHELL;
    if (auth_peer(p.pid, p.uid, 1000, "/usr/bin/leon-compositor", "/usr/sbin/leond"))
        return AUTH_COMPOSITOR;
    if (auth_peer(p.pid, p.uid, 11000, "/usr/bin/leon-settings", "/usr/sbin/leon-launchd"))
        return AUTH_SETTINGS;
    return AUTH_NONE;
}
static int listener(struct auth_daemon *d) {
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    if (strlen(d->socket) >= sizeof(a.sun_path))
        return -1;
    strcpy(a.sun_path, d->socket);
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", d->socket);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return -1;
    *slash = 0;
    struct stat info;
    if (lm_mkdir(parent, 0755) || lstat(parent, &info) || info.st_uid || (info.st_mode & 022) ||
        chmod(parent, 0755))
        return -1;
    if (!lstat(d->socket, &info)) {
        if (!S_ISSOCK(info.st_mode) || info.st_uid || unlink(d->socket))
            return -1;
    } else if (errno != ENOENT)
        return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    mode_t mask = umask(0111);
    int result = fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof(a)) || listen(fd, AUTH_CLIENTS);
    umask(mask);
    if (result) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    return fd;
}
static void receive(struct auth_daemon *d, int i) {
    struct auth_client *c = &d->clients[i];
    /* recvmsg makes supplied SCM_RIGHTS visible and closes them before rejection. */
    char control[CMSG_SPACE(sizeof(int) * 8)];
    struct iovec v = {c->request + c->received, sizeof(c->request) - 1 - c->received};
    struct msghdr m = {
        .msg_iov = &v, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof(control)};
    ssize_t n = recvmsg(c->fd, &m, MSG_CMSG_CLOEXEC);
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
        return;
    int ancillary = (m.msg_flags & MSG_CTRUNC) != 0;
    for (struct cmsghdr *h = n > 0 ? CMSG_FIRSTHDR(&m) : NULL; h; h = CMSG_NXTHDR(&m, h)) {
        ancillary = 1;
        if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_RIGHTS) {
            size_t count = (h->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t j = 0; j < count; ++j) {
                int fd;
                memcpy(&fd, (char *)CMSG_DATA(h) + j * sizeof(int), sizeof(fd));
                close(fd);
            }
        }
    }
    if (n <= 0 || ancillary || memchr(c->request + c->received, 0, (size_t)(n > 0 ? n : 0))) {
        auth_close(d, i);
        return;
    }
    c->received += (size_t)n;
    char *end = memchr(c->request, '\n', c->received);
    if (end) {
        /* Exactly one outstanding command per connection; monitor clients wait for a reply. */
        if ((size_t)(end - c->request) + 1 != c->received || c->pending || c->size) {
            auth_close(d, i);
            return;
        }
        *end = 0;
        auth_dispatch(d, i);
    } else if (c->received == sizeof(c->request) - 1)
        auth_close(d, i);
}
int auth_server(struct auth_daemon *d) {
    for (int i = 0; i < AUTH_CLIENTS; ++i)
        d->clients[i].fd = -1;
    d->watch = -1;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL))
        return 1;
    int signals = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK), server = listener(d);
    if (signals < 0 || server < 0 || auth_worker_start(d))
        return 1;
    leon_service_ready();
    int running = 1;
    while (running) {
        leon_service_watchdog();
        auth_tick(d);
        struct pollfd fds[AUTH_CLIENTS + 3] = {
            {server, POLLIN, 0}, {signals, POLLIN, 0}, {d->job.event, POLLIN, 0}};
        for (int i = 0; i < AUTH_CLIENTS; ++i) {
            struct auth_client *c = &d->clients[i];
            if (c->fd >= 0 && !c->watching && auth_now() > c->deadline)
                auth_close(d, i);
            if (c->fd >= 0 && c->watching && c->size && auth_now() > c->deadline)
                auth_close(d, i);
            /* Do not read a half-close while deriving or before sending its reply.
             * A queued heartbeat must finish before the next monitor command. */
            fds[i + 3] = (struct pollfd){c->fd,
                                         (short)(c->size      ? POLLOUT
                                                 : c->pending ? 0
                                                              : POLLIN),
                                         0};
        }
        if (poll(fds, AUTH_CLIENTS + 3, 50) < 0 && errno != EINTR)
            break;
        if (fds[1].revents & POLLIN)
            running = 0;
        if (fds[2].revents & POLLIN)
            auth_complete(d);
        for (int i = 0; i < AUTH_CLIENTS; ++i) {
            struct auth_client *c = &d->clients[i];
            short events = fds[i + 3].revents;
            if (c->fd < 0)
                continue;
            if (events & (POLLERR | POLLNVAL)) {
                auth_close(d, i);
                continue;
            }
            if (events & POLLOUT && c->size) {
                ssize_t n = send(c->fd, c->reply + c->sent, c->size - c->sent, MSG_NOSIGNAL);
                if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    auth_close(d, i);
                    continue;
                }
                if (n > 0)
                    c->sent += (size_t)n;
                if (c->sent == c->size) {
                    credential_clear(c->reply, sizeof(c->reply));
                    c->sent = c->size = 0;
                    if (!c->watching) {
                        auth_close(d, i);
                        continue;
                    }
                }
            }
            if (!c->size && !c->pending && (events & (POLLIN | POLLHUP)))
                receive(d, i);
        }
        if (fds[0].revents & POLLIN) {
            int fd = accept4(server, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (fd < 0)
                continue;
            struct ucred peer;
            socklen_t size = sizeof(peer);
            if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size)) {
                close(fd);
                continue;
            }
            int role = peer_role(d, peer);
            /* Reserve monitor/control capacity even with slow Settings clients. */
            int limit =
                role == AUTH_COMPOSITOR || role == AUTH_ROOT ? AUTH_CLIENTS : AUTH_CLIENTS - 4;
            int slot = 0;
            while (slot < limit && d->clients[slot].fd >= 0)
                ++slot;
            if (role == AUTH_NONE || slot == limit) {
                close(fd);
                continue;
            }
            d->clients[slot] = (struct auth_client){.fd = fd,
                                                    .peer = peer,
                                                    .role = role,
                                                    .sequence = ++d->sequence,
                                                    .deadline = auth_now() + 1000};
        }
    }
    auth_set_locked(d);
    for (int i = 0; i < AUTH_CLIENTS; ++i)
        auth_close(d, i);
    auth_worker_stop(d);
    close(server);
    close(signals);
    unlink(d->socket);
    return 0;
}
