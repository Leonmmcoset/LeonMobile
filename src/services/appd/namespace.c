#define _GNU_SOURCE
#include "namespace.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

struct packet { int kind, value; };
pid_t leon_namespace_clone(void) {
#if !defined(__aarch64__) && !defined(__x86_64__)
#error Unsupported namespace architecture
#endif
    /* No CLONE_VM: the ordinary fork stack and libc state remain private. */
    return (pid_t)syscall(SYS_clone, CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC | SIGCHLD,
                         NULL, NULL, NULL, 0);
}
int leon_namespace_close_fds(int first, const int *keep, size_t count) {
    if (first < 0) { errno = EINVAL; return -1; }
    unsigned start = (unsigned)first;
    for (;;) {
        unsigned next = UINT_MAX;
        for (size_t i = 0; i < count; ++i)
            if (keep[i] >= 0 && (unsigned)keep[i] >= start && (unsigned)keep[i] < next)
                next = (unsigned)keep[i];
        if (next > start && syscall(SYS_close_range, start, next == UINT_MAX ? next : next - 1, 0))
            return -1;
        if (next == UINT_MAX) return 0;
        start = next + 1;
    }
}
static int send_packet(int fd, int kind, int value) {
    const struct packet packet = {kind, value};
    ssize_t sent;
    do {
        sent = send(fd, &packet, sizeof(packet), MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (sent < 0 && errno == EINTR);
    return sent == sizeof(packet) ? 0 : -1;
}
int leon_namespace_identify(int control) { return send_packet(control, LEON_NS_STARTED, 0); }
int leon_namespace_signal(int control, int signal) {
    if (signal != SIGTERM && signal != SIGKILL) {
        errno = EINVAL;
        return -1;
    }
    return send_packet(control, LEON_NS_SIGNAL, signal);
}
int leon_namespace_receive(int control, int kind, pid_t expected_pid, uid_t uid,
                           pid_t *sender, int *status) {
    struct packet packet = {0};
    struct iovec vector = {&packet, sizeof(packet)};
    union {
        struct cmsghdr align;
        char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(8 * sizeof(int))];
    } ancillary = {0};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
                             .msg_control = ancillary.bytes, .msg_controllen = sizeof(ancillary)};
    ssize_t received;
    do {
        received = recvmsg(control, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    } while (received < 0 && errno == EINTR);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 0;
    struct ucred *credentials = NULL;
    int invalid = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); received >= 0 && c;
         c = CMSG_NXTHDR(&message, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS &&
            c->cmsg_len == CMSG_LEN(sizeof(struct ucred)) && !credentials)
            credentials = (struct ucred *)CMSG_DATA(c);
        else {
            invalid = 1;
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS &&
                c->cmsg_len >= CMSG_LEN(0))
                for (size_t i = 0; i < (c->cmsg_len - CMSG_LEN(0)) / sizeof(int); ++i)
                    close(((int *)CMSG_DATA(c))[i]);
        }
    }
    if (received != sizeof(packet) || invalid || message.msg_flags & (MSG_TRUNC | MSG_CTRUNC) ||
        packet.kind != kind || !credentials || credentials->uid != uid || credentials->gid != uid ||
        credentials->pid <= 0 || (expected_pid && credentials->pid != expected_pid)) {
        errno = EPROTO;
        return -1;
    }
    if (sender) *sender = credentials->pid;
    if (status) *status = packet.value;
    return 1;
}
static void supervise(int control, int signals, pid_t app) {
    int exited = 0;
    for (;;) {
        if (!exited) {
            for (;;) {
                siginfo_t info = {0};
                if (waitid(P_ALL, 0, &info, WEXITED | WNOHANG | WNOWAIT)) {
                    if (errno == EINTR) continue;
                    _exit(125);
                }
                if (!info.si_pid) break;
                if (info.si_pid == app) {
                    int status = info.si_code == CLD_EXITED ? info.si_status << 8 :
                                 info.si_status | (info.si_code == CLD_DUMPED ? 0x80 : 0);
                    /* Keep the actual ELF's zombie: its host PID cannot be
                     * reused while the broker is still tracking this task. */
                    if (send_packet(control, LEON_NS_EXITED, status)) _exit(125);
                    kill(-1, SIGKILL);
                    exited = 1;
                    break;
                }
                while (waitpid(info.si_pid, NULL, 0) < 0 && errno == EINTR) {}
            }
        }
        struct pollfd fds[] = {{control, POLLIN, 0}, {exited ? -1 : signals, POLLIN, 0}};
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            _exit(125);
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            struct packet packet;
            ssize_t n = recv(control, &packet, sizeof(packet), MSG_DONTWAIT | MSG_TRUNC);
            if (n != sizeof(packet) || packet.kind != LEON_NS_SIGNAL ||
                (packet.value != SIGTERM && packet.value != SIGKILL))
                _exit(0); /* A dead broker tears down the complete PID namespace. */
            if (!exited) kill(-app, packet.value);
        }
        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo info;
            while (read(signals, &info, sizeof(info)) == sizeof(info)) {}
        }
    }
}
int leon_namespace_fork(int control, int exec_ack, uid_t uid) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    struct sigaction action = {.sa_handler = SIG_DFL};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGCHLD, &action, NULL) || sigprocmask(SIG_SETMASK, &mask, NULL))
        return -1;
    pid_t app = fork();
    if (app <= 0)
        return (int)app;
    int keep[] = {control, exec_ack};
    if (leon_namespace_close_fds(0, keep, 2) || setgroups(0, NULL) || setgid(uid) || setuid(uid) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || prctl(PR_SET_DUMPABLE, 0) ||
        prctl(PR_SET_PDEATHSIG, SIGKILL) || prctl(PR_SET_NAME, "leon-app-init")) {
        int cause = errno;
        (void)write(exec_ack, &cause, sizeof(cause));
        _exit(125);
    }
    int signals = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signals < 0) {
        int cause = errno;
        (void)write(exec_ack, &cause, sizeof(cause));
        _exit(125);
    }
    close(exec_ack);
    supervise(control, signals, app);
    _exit(125);
}
