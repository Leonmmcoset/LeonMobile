#include "../serviced/notify.h"
#include "daemon.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_LIMIT 16
#define CLIENT_TIMEOUT_MS 1000

struct client {
    int fd;
    uid_t uid;
    char request[LM_REQUEST_LIMIT + 1];
    size_t received;
    char *reply;
    size_t sent;
    size_t reply_size;
    int draining;
    int64_t deadline;
};

long long lm_now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void close_client(struct client *client) {
    close(client->fd);
    free(client->reply);
    *client = (struct client){.fd = -1};
}

static void set_reply(struct client *client, char *reply) {
    if (!reply) {
        close_client(client);
        return;
    }
    client->reply = reply;
    client->reply_size = strlen(reply);
    client->deadline = lm_now_ms() + CLIENT_TIMEOUT_MS;
}

static void receive_request(struct client *client, struct lm_daemon *daemon) {
    if (client->draining) {
        // Consume trailing input after closing the response half. Closing a
        // Unix socket with unread bytes would reset an otherwise complete reply.
        char discard[4096];
        ssize_t count = recv(client->fd, discard, sizeof(discard), 0);
        if (!count || (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK))
            close_client(client);
        return;
    }
    ssize_t count = recv(client->fd, client->request + client->received,
                         sizeof(client->request) - client->received, 0);
    if (count < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            close_client(client);
        return;
    }
    if (!count) {
        set_reply(client, lm_error("request must end with a newline"));
        return;
    }
    client->received += (size_t)count;
    char *end = memchr(client->request, '\n', client->received);
    if (end)
        set_reply(client, lm_dispatch(daemon, client->uid, client->request,
                                      (size_t)(end - client->request)));
    else if (client->received == sizeof(client->request))
        set_reply(client, lm_error("request too large"));
}

static void send_reply(struct client *client) {
    ssize_t count = send(client->fd, client->reply + client->sent,
                         client->reply_size - client->sent, MSG_NOSIGNAL);
    if (count < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            close_client(client);
        return;
    }
    client->sent += (size_t)count;
    if (!count)
        close_client(client);
    else if (client->sent == client->reply_size) {
        if (shutdown(client->fd, SHUT_WR))
            close_client(client);
        else
            client->draining = 1;
    }
}

static int bind_socket(int fd, const char *path) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(address.sun_path, path);
    struct stat info;
    if (!lstat(path, &info)) {
        // Recover a socket left after a crash, without replacing a live daemon
        // or removing an unrelated file/symlink.
        if (!S_ISSOCK(info.st_mode)) {
            errno = EEXIST;
            return -1;
        }
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (probe < 0)
            return -1;
        int connected = connect(probe, (struct sockaddr *)&address, sizeof(address));
        int error = errno;
        close(probe);
        if (!connected || (error != ECONNREFUSED && error != ENOENT)) {
            errno = EADDRINUSE;
            return -1;
        }
        if (unlink(path) && errno != ENOENT)
            return -1;
    } else if (errno != ENOENT)
        return -1;
    mode_t previous = umask(0111);
    int result = bind(fd, (struct sockaddr *)&address, sizeof(address));
    umask(previous);
    return result;
}

static void accept_clients(int server, struct client *clients) {
    for (int accepted = 0; accepted < CLIENT_LIMIT; ++accepted) {
        int fd = accept4(server, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0)
            break;
        struct ucred peer;
        socklen_t size = sizeof(peer);
        int slot = 0;
        while (slot < CLIENT_LIMIT && clients[slot].fd >= 0)
            ++slot;
        if (slot == CLIENT_LIMIT || getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size)) {
            close(fd);
            continue;
        }
        clients[slot] =
            (struct client){.fd = fd, .uid = peer.uid, .deadline = lm_now_ms() + CLIENT_TIMEOUT_MS};
    }
}

int lm_serve(struct lm_daemon *daemon) {
    const struct lm_options *options = &daemon->options;
    struct client clients[CLIENT_LIMIT];
    for (int i = 0; i < CLIENT_LIMIT; ++i)
        clients[i] = (struct client){.fd = -1};
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL)) {
        perror("system service: signals");
        return 1;
    }
    int signals = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (signals < 0 || server < 0 || bind_socket(server, options->socket_path)) {
        perror("system service: socket");
        if (signals >= 0)
            close(signals);
        if (server >= 0)
            close(server);
        return 1;
    }
    if (listen(server, CLIENT_LIMIT)) {
        perror("system service: listen");
        close(signals);
        close(server);
        unlink(options->socket_path);
        return 1;
    }
    int result = 0;
    leon_service_ready();
    for (;;) {
        leon_service_watchdog();
        if (daemon->tick)
            daemon->tick(daemon);
        struct pollfd fds[CLIENT_LIMIT + 2] = {{server, POLLIN, 0}, {signals, POLLIN, 0}};
        for (int i = 0; i < CLIENT_LIMIT; ++i) {
            if (clients[i].fd >= 0 && lm_now_ms() >= clients[i].deadline)
                close_client(&clients[i]);
            fds[i + 2] = (struct pollfd){
                clients[i].fd, clients[i].reply && !clients[i].draining ? POLLOUT : POLLIN, 0};
        }
        if (poll(fds, CLIENT_LIMIT + 2, 100) < 0) {
            if (errno == EINTR)
                continue;
            perror("system service: poll");
            result = 1;
            break;
        }
        if (fds[1].revents & POLLIN)
            break;
        for (int i = 0; i < CLIENT_LIMIT; ++i) {
            short events = fds[i + 2].revents;
            if (clients[i].fd < 0)
                continue;
            if (events & (POLLERR | POLLNVAL))
                close_client(&clients[i]);
            else if (clients[i].reply && !clients[i].draining && events & POLLOUT)
                send_reply(&clients[i]);
            else if ((!clients[i].reply || clients[i].draining) && events & (POLLIN | POLLHUP))
                receive_request(&clients[i], daemon);
        }
        if (fds[0].revents & POLLIN)
            accept_clients(server, clients);
    }
    for (int i = 0; i < CLIENT_LIMIT; ++i)
        if (clients[i].fd >= 0)
            close_client(&clients[i]);
    close(signals);
    close(server);
    unlink(options->socket_path);
    return result;
}
