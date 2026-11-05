#include "logd.h"
#include "../serviced/notify.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#define CLIENTS 12
#define WORKERS 6
struct client { int fd; struct ucred peer; unsigned used; int64_t deadline; char text[4096]; };
struct worker { pid_t pid; int64_t deadline; };
static struct log_state state;
static int bind_socket(const char *dir, const char *name, int type, mode_t mode) {
    char path[108];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
        return -1;
    struct stat st;
    if (!lstat(path, &st)) {
        if (!S_ISSOCK(st.st_mode) || st.st_uid || unlink(path))
            return -1;
    } else if (errno != ENOENT)
        return -1;
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    strcpy(a.sun_path, path);
    int fd = socket(AF_UNIX, type | SOCK_CLOEXEC | SOCK_NONBLOCK, 0), one = 1;
    if (fd < 0)
        return -1;
    if ((type == SOCK_DGRAM && setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one))) ||
        bind(fd, (struct sockaddr *)&a, sizeof(a)) || chmod(path, mode) ||
        (type == SOCK_STREAM && listen(fd, CLIENTS))) {
        close(fd);
        return -1;
    }
    return fd;
}
static void start_writer(struct log_state *s) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair))
        return;
    /* Absorb the boot-ring replay burst while the disk worker starts. The
     * kernel still caps this queue; a stopped worker cannot grow it. */
    int buffer = 256 * 1024;
    setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
    pid_t parent = getpid(), pid = fork();
    if (!pid) {
        log_child(pair[1], parent);
        log_writer(&s->options, 3);
        _exit(0);
    }
    close(pair[1]);
    if (pid < 0) {
        close(pair[0]);
        return;
    }
    s->writer = pair[0];
    s->writer_pid = pid;
    s->writer_seen = 0;
    s->ack.error = EAGAIN;
}
static void close_client(struct client *c) {
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
}
static void accept_clients(int server, struct client *clients) {
    for (int budget = 0; budget < CLIENTS; ++budget) {
        int fd = accept4(server, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0)
            break;
        struct ucred peer;
        socklen_t length = sizeof(peer);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length)) {
            close(fd);
            continue;
        }
        int trusted = !peer.uid || (peer.uid == 11000 &&
                      (state.options.test || auth_peer(peer.pid, peer.uid, 11000,
                       "/usr/bin/leon-settings", "/usr/sbin/leon-launchd")));
        int slot = 0;
        /* Reserve two request/worker slots for trusted diagnostics. */
        while (slot < (trusted ? CLIENTS : CLIENTS - 2) && clients[slot].fd >= 0)
            ++slot;
        if (slot == (trusted ? CLIENTS : CLIENTS - 2)) {
            close(fd);
            continue;
        }
        clients[slot] = (struct client){.fd = fd, .peer = peer,
            .deadline = log_clock_ns(CLOCK_MONOTONIC) / 1000000 + 700};
    }
}
static void request(struct client *c, struct worker *workers) {
    char controls[CMSG_SPACE(sizeof(int) * 16)];
    struct iovec v = {c->text + c->used, sizeof(c->text) - c->used};
    struct msghdr m = {.msg_iov = &v, .msg_iovlen = 1,
                       .msg_control = controls, .msg_controllen = sizeof(controls)};
    ssize_t n = recvmsg(c->fd, &m, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return;
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
    if (n <= 0 || invalid || memchr(c->text + c->used, 0, (size_t)n)) {
        close_client(c);
        return;
    }
    c->used += (unsigned)n;
    char *end = memchr(c->text, '\n', c->used);
    if (!end) {
        if (c->used == sizeof(c->text))
            close_client(c);
        return;
    }
    if (end != c->text + c->used - 1) {
        close_client(c);
        return;
    }
    int trusted = !c->peer.uid || (c->peer.uid == 11000 &&
                  (state.options.test || auth_peer(c->peer.pid, c->peer.uid, 11000,
                   "/usr/bin/leon-settings", "/usr/sbin/leon-launchd")));
    int slot = 0;
    while (slot < (trusted ? WORKERS : WORKERS - 2) && workers[slot].pid)
        ++slot;
    if (slot < (trusted ? WORKERS : WORKERS - 2)) {
        pid_t parent = getpid(), pid = fork();
        if (!pid) {
            log_child(c->fd, parent);
            log_query(&state, c->peer, c->text, c->used - 1, 3);
            _exit(0);
        }
        if (pid > 0)
            workers[slot] = (struct worker){pid, log_clock_ns(CLOCK_MONOTONIC) / 1000000 + 2500};
    }
    close_client(c);
}
int main(int argc, char **argv) {
    state.options = (struct log_options){.runtime = LEON_LOG_DIRECTORY,
        .state = "/data/leon/logs", .auth = LEON_AUTH_SOCKET,
        .crash = "/run/leon/crash.sock", .services = "/run/leon/services.sock", .kmsg = "/dev/kmsg"};
    state.writer = state.kernel = -1;
    umask(077);
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--test")) { state.options.test = 1; continue; }
        if (i + 1 == argc)
            return 2;
        const char **field = !strcmp(argv[i], "--runtime") ? &state.options.runtime
            : !strcmp(argv[i], "--state") ? &state.options.state
            : !strcmp(argv[i], "--auth") ? &state.options.auth
            : !strcmp(argv[i], "--crash") ? &state.options.crash
            : !strcmp(argv[i], "--services") ? &state.options.services
            : !strcmp(argv[i], "--kmsg") ? &state.options.kmsg
            : !strcmp(argv[i], "--boot-id") ? &state.options.boot : NULL;
        if (!field)
            return 2;
        *field = argv[++i];
    }
    if (geteuid() || (!state.options.test && (state.options.boot || strcmp(state.options.kmsg, "/dev/kmsg"))))
        return 2;
    if (state.options.boot) {
        if (strlen(state.options.boot) != 36)
            return 2;
        strcpy(state.boot, state.options.boot);
    } else {
        char text[64];
        if (lm_read_text("/proc/sys/kernel/random/boot_id", text, sizeof(text)) || strlen(text) < 36)
            return 1;
        memcpy(state.boot, text, 36);
    }
    int dir = log_secure_directory(state.options.runtime, 0755);
    if (dir < 0) { perror("logd runtime"); return 1; }
    int lock = openat(dir, "lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat info;
    if (lock < 0 || fstat(lock, &info) || !S_ISREG(info.st_mode) || info.st_uid ||
        info.st_nlink != 1 || (info.st_mode & 0777) != 0600 || flock(lock, LOCK_EX | LOCK_NB))
        return 1;
    int cursor = openat(dir, "kernel.cursor", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (cursor >= 0 && (fstat(cursor, &info) || !S_ISREG(info.st_mode) || info.st_uid ||
                       info.st_nlink != 1 || (info.st_mode & 0777) != 0600)) {
        close(cursor);
        cursor = -1;
    }
    close(dir);
    int server = bind_socket(state.options.runtime, "control.sock", SOCK_STREAM, 0666);
    int public = bind_socket(state.options.runtime, "write.sock", SOCK_DGRAM, 0666);
    int capture = bind_socket(state.options.runtime, "capture.sock", SOCK_DGRAM, 0600);
    if (server < 0 || public < 0 || capture < 0) { perror("logd sockets"); return 1; }
    if (*state.options.kmsg && (!state.options.test || strcmp(state.options.kmsg, "/dev/kmsg")))
        state.kernel = open(state.options.kmsg, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    signal(SIGPIPE, SIG_IGN);
    int signals = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signals < 0)
        return 1;
    struct client clients[CLIENTS];
    struct worker workers[WORKERS] = {0};
    for (int i = 0; i < CLIENTS; ++i)
        clients[i].fd = -1;
    start_writer(&state);
    leon_service_ready();
    fprintf(stderr, "logd: ready; bounded journal, credential-checked queries\n");
    int64_t next_writer = 0;
    for (;;) {
        leon_service_watchdog();
        int64_t now = log_clock_ns(CLOCK_MONOTONIC) / 1000000;
        if (state.writer_pid <= 0 && now >= next_writer) {
            start_writer(&state);
            next_writer = now + 1000;
        }
        struct pollfd fds[CLIENTS + 5] = {{server, POLLIN, 0}, {public, POLLIN, 0},
            {capture, POLLIN, 0}, {signals, POLLIN, 0}, {state.writer, POLLIN, 0}};
        for (int i = 0; i < CLIENTS; ++i) {
            if (clients[i].fd >= 0 && now >= clients[i].deadline)
                close_client(&clients[i]);
            fds[i + 5] = (struct pollfd){clients[i].fd, POLLIN, 0};
        }
        if (state.kernel >= 0)
            log_kernel(&state, cursor);
        int result = poll(fds, CLIENTS + 5, 50);
        if (result < 0 && errno != EINTR)
            break;
        if (fds[3].revents & POLLIN)
            break;
        if (fds[2].revents & POLLIN)
            log_ingest(&state, capture, 1);
        if (fds[1].revents & POLLIN)
            log_ingest(&state, public, 0);
        if (fds[4].revents & POLLIN)
            for (int budget = 0; budget < 256; ++budget) {
                struct log_ack ack;
                if (recv(state.writer, &ack, sizeof(ack), MSG_DONTWAIT) != sizeof(ack))
                    break;
                state.ack = ack;
                state.writer_seen = now;
            }
        for (int i = 0; i < CLIENTS; ++i)
            if (clients[i].fd >= 0 && fds[i + 5].revents & (POLLIN | POLLHUP | POLLERR))
                request(&clients[i], workers);
        if (fds[0].revents & POLLIN)
            accept_clients(server, clients);
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            if (pid == state.writer_pid) {
                close(state.writer);
                state.writer = -1;
                state.writer_pid = 0;
            }
            for (int i = 0; i < WORKERS; ++i)
                if (workers[i].pid == pid)
                    workers[i].pid = 0;
        }
        for (int i = 0; i < WORKERS; ++i)
            if (workers[i].pid && now >= workers[i].deadline)
                kill(workers[i].pid, SIGKILL);
    }
    for (int i = 0; i < CLIENTS; ++i)
        close_client(&clients[i]);
    for (int i = 0; i < WORKERS; ++i)
        if (workers[i].pid)
            kill(workers[i].pid, SIGKILL);
    /* Give the writer a short flush opportunity, without blocking shutdown. */
    if (state.writer >= 0) {
        close(state.writer);
        for (int i = 0; i < 20; ++i) {
            if (waitpid(state.writer_pid, NULL, WNOHANG) == state.writer_pid) {
                state.writer_pid = 0;
                break;
            }
            usleep(10000);
        }
        if (state.writer_pid > 0)
            kill(state.writer_pid, SIGKILL);
    }
    return 0;
}
