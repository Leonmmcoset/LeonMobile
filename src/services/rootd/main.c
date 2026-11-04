#include "../serviced/notify.h"
#include "rootd.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#define WORKERS 8
int root_reply(int fd, lm_json *reply, int descriptor) {
    char *text = lm_dump(reply);
    lm_free(reply);
    int result = text ? root_send(fd, text, descriptor) : -1;
    free(text);
    return result;
}
static int settings_peer(const struct root_options *o, struct ucred peer) {
    if (!peer.uid)
        return 1;
    if (peer.uid != 11000)
        return 0;
    if (o->test)
        return 1;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/exe", peer.pid);
    struct stat caller, installed;
    return !stat(path, &caller) && !stat("/usr/bin/leon-settings", &installed) &&
           caller.st_dev == installed.st_dev && caller.st_ino == installed.st_ino;
}
static void clear_passwords(lm_json *request) {
    for (const char **field = (const char *[]){"password", "new_password", NULL}; *field; ++field) {
        char *value = (char *)lm_string(lm_get(request, *field), "");
        if (*value)
            root_clear(value, strlen(value));
    }
}
static void serve(const struct root_options *o, int socket, int notice, struct ucred peer) {
    char text[LEON_ROOT_MESSAGE];
    if (root_receive(socket, text, sizeof(text), NULL))
        return;
    lm_json *request = lm_parse(text, strlen(text));
    root_clear(text, sizeof(text));
    if (!lm_is_object(request)) {
        lm_free(request);
        root_reply(socket, lm_failure("invalid request"), -1);
        return;
    }
    const char *op = lm_string(lm_get(request, "op"), "");
    int settings = settings_peer(o, peer);
    int terminal = peer.uid == 0 || peer.uid == 1000 || peer.uid == 11005;
    lm_json *reply = NULL;
    struct root_policy policy = {0};
    int lock = -1;
    if ((!settings && !terminal) || (!strcmp(op, "exec") && !terminal)) {
        reply = lm_failure("caller may not request root access");
        goto done;
    }
    if (root_policy_load(o, &policy)) {
        reply = lm_failure("root policy is invalid; access is locked");
        goto done;
    }
    if (!strcmp(op, "status")) {
        reply = root_policy_status(&policy);
        goto done;
    }
    if (strcmp(op, "exec") && !settings) {
        reply = lm_failure("only the installed settings application may manage root");
        goto done;
    }
    int configure = !strcmp(op, "enable") || !strcmp(op, "disable") || !strcmp(op, "password");
    if (!configure && !strcmp(op, "inspect")) {
        reply = root_management(o, request, 0);
        goto done;
    }
    lock = root_policy_lock(o);
    if (lock < 0 || root_policy_load(o, &policy)) {
        reply = lm_failure("root policy is busy or unavailable");
        goto done;
    }
    const char *password = lm_string(lm_get(request, "password"), "");
    if (!strcmp(op, "enable") && !policy.present) {
        if (root_set_password(&policy, password)) {
            reply =
                lm_failure("root password must contain 8 to 128 bytes without control characters");
            goto done;
        }
    } else {
        if ((!configure && !policy.enabled) || root_authenticate(o, &policy, password)) {
            reply = lm_failure(policy.enabled || configure
                                   ? "root authentication failed or temporarily locked"
                                   : "root access is disabled");
            goto done;
        }
    }
    if (configure) {
        if (!strcmp(op, "password") &&
            root_set_password(&policy, lm_string(lm_get(request, "new_password"), ""))) {
            reply =
                lm_failure("new password must contain 8 to 128 bytes without control characters");
            goto done;
        }
        if (strcmp(op, "password"))
            policy.enabled = !strcmp(op, "enable");
        if (policy.generation == INT64_MAX) {
            reply = lm_failure("root policy generation exhausted");
            goto done;
        }
        ++policy.generation;
        reply = root_policy_save(o, &policy) ? lm_failure("cannot persist root policy")
                                             : root_policy_status(&policy);
        goto done;
    }
    if (!strcmp(op, "exec")) {
        int64_t generation = policy.generation;
        clear_passwords(request);
        root_clear(&policy, sizeof(policy));
        close(lock);
        lock = -1;
        root_session(o, socket, notice, peer, request, generation);
    } else
        reply = root_management(o, request, 1);
done:
    if (lock >= 0)
        close(lock);
    clear_passwords(request);
    lm_free(request);
    root_clear(&policy, sizeof(policy));
    if (reply)
        root_reply(socket, reply, -1);
}
struct worker {
    pid_t pid;
    int notice;
    int64_t generation, deadline;
};
static int listener(const struct root_options *o) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(o->socket) >= sizeof(address.sun_path))
        return -1;
    strcpy(address.sun_path, o->socket);
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", o->socket);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return -1;
    *slash = 0;
    if (lm_mkdir(parent, 0755))
        return -1;
    struct stat info;
    if (lstat(parent, &info) || info.st_uid || (info.st_mode & 022))
        return -1;
    /* umask 077 protects persistent state, but allowed clients must traverse this socket directory. */
    if (chmod(parent, 0755))
        return -1;
    if (!lstat(o->socket, &info)) {
        if (!S_ISSOCK(info.st_mode) || info.st_uid || unlink(o->socket))
            return -1;
    } else if (errno != ENOENT)
        return -1;
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    mode_t mask = umask(0111);
    int result = server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) ||
                 listen(server, WORKERS);
    umask(mask);
    if (result) {
        if (server >= 0)
            close(server);
        return -1;
    }
    return server;
}
int main(int argc, char **argv) {
    struct root_options o = {
        "/data/leon/rootd",   LEON_ROOT_SOCKET,          "/proc", "/lib/modules",
        "/etc/leon/services", "/run/leon/services.sock", 0};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--test")) {
            o.test = 1;
            continue;
        }
        const char **field = !strcmp(argv[i], "--state-dir")        ? &o.directory
                             : !strcmp(argv[i], "--socket")         ? &o.socket
                             : !strcmp(argv[i], "--proc")           ? &o.proc
                             : !strcmp(argv[i], "--modules")        ? &o.modules
                             : !strcmp(argv[i], "--services")       ? &o.services
                             : !strcmp(argv[i], "--service-socket") ? &o.service_socket
                                                                    : NULL;
        if (!field || i + 1 == argc)
            return 2;
        *field = argv[++i];
    }
    if (geteuid() || (o.test && (!strcmp(o.proc, "/proc") || !strcmp(o.modules, "/lib/modules"))))
        return 2;
    umask(077);
    struct rlimit limit = {0, 0};
    setrlimit(RLIMIT_CORE, &limit);
    prctl(PR_SET_DUMPABLE, 0);
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    int singleton = root_policy_lock(&o);
    if (singleton < 0)
        return 1;
    char name[PATH_MAX];
    snprintf(name, sizeof(name), "%s/daemon.lock", o.directory);
    int instance = open(name, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat instance_info;
    close(singleton);
    if (instance < 0 || fstat(instance, &instance_info) || !S_ISREG(instance_info.st_mode) ||
        instance_info.st_uid || instance_info.st_nlink != 1 || (instance_info.st_mode & 077) ||
        flock(instance, LOCK_EX | LOCK_NB))
        return 1;
    sigset_t mask;
    sigemptyset(&mask);
    for (const int *s = (int[]){SIGTERM, SIGINT, SIGCHLD, 0}; *s; ++s)
        sigaddset(&mask, *s);
    if (sigprocmask(SIG_BLOCK, &mask, NULL))
        return 1;
    int signals = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK), server = listener(&o);
    if (signals < 0 || server < 0) {
        perror("rootd socket");
        return 1;
    }
    root_session_cleanup(&o, 0);
    struct worker workers[WORKERS] = {0};
    int running = 1;
    leon_service_ready();
    while (running) {
        leon_service_watchdog();
        struct pollfd fds[WORKERS + 2] = {{server, POLLIN, 0}, {signals, POLLIN, 0}};
        for (size_t i = 0; i < WORKERS; ++i)
            fds[i + 2] = (struct pollfd){workers[i].pid ? workers[i].notice : -1, POLLIN, 0};
        if (poll(fds, WORKERS + 2, 100) < 0 && errno != EINTR)
            break;
        struct root_policy policy;
        int valid = !root_policy_load(&o, &policy);
        for (size_t i = 0; i < WORKERS; ++i) {
            struct worker *w = &workers[i];
            if (!w->pid)
                continue;
            if (fds[i + 2].revents & POLLIN) {
                struct root_session_notice notice;
                if (read(w->notice, &notice, sizeof(notice)) == sizeof(notice))
                    w->generation = notice.generation;
            }
            if ((w->generation &&
                 (!valid || !policy.enabled || w->generation != policy.generation)) ||
                (!w->generation && lm_now_ms() > w->deadline)) {
                root_session_cleanup(&o, w->pid);
                kill(-w->pid, SIGKILL);
                kill(w->pid, SIGKILL);
            }
        }
        int status;
        pid_t exited;
        while ((exited = waitpid(-1, &status, WNOHANG)) > 0)
            for (size_t i = 0; i < WORKERS; ++i)
                if (workers[i].pid == exited) {
                    root_session_cleanup(&o, exited);
                    close(workers[i].notice);
                    workers[i] = (struct worker){0};
                }
        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo info;
            while (read(signals, &info, sizeof(info)) == sizeof(info))
                if (info.ssi_signo != SIGCHLD)
                    running = 0;
        }
        if (running && fds[0].revents & POLLIN) {
            int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
            if (client < 0)
                continue;
            size_t slot = 0;
            while (slot < WORKERS && workers[slot].pid)
                ++slot;
            struct ucred peer;
            socklen_t size = sizeof(peer);
            int pipe[2] = {-1, -1};
            if (slot == WORKERS || getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &size) ||
                pipe2(pipe, O_CLOEXEC | O_NONBLOCK)) {
                close(client);
                continue;
            }
            pid_t parent = getpid(), child = fork();
            if (!child) {
                setsid();
                close(pipe[0]);
                close(server);
                close(signals);
                close(instance);
                for (size_t i = 0; i < WORKERS; ++i)
                    if (workers[i].pid)
                        close(workers[i].notice);
                sigset_t empty;
                sigemptyset(&empty);
                sigprocmask(SIG_SETMASK, &empty, NULL);
                if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
                    _exit(126);
                struct timeval timeout = {.tv_sec = 3};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
                serve(&o, client, pipe[1], peer);
                close(client);
                close(pipe[1]);
                _exit(0);
            }
            close(client);
            close(pipe[1]);
            if (child < 0)
                close(pipe[0]);
            else
                workers[slot] = (struct worker){child, pipe[0], 0, lm_now_ms() + 15000};
        }
    }
    for (size_t i = 0; i < WORKERS; ++i)
        if (workers[i].pid) {
            root_session_cleanup(&o, workers[i].pid);
            kill(-workers[i].pid, SIGKILL);
            kill(workers[i].pid, SIGKILL);
        }
    while (waitpid(-1, NULL, 0) > 0 || errno == EINTR) {
    }
    close(server);
    close(signals);
    close(instance);
    unlink(o.socket);
    return 0;
}
