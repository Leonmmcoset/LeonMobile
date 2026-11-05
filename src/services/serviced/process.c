#include "manager.h"
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
void manager_start(struct manager *manager, struct service *s) {
    if (s->pid || !s->wanted || service_now_ms() < s->next_ms ||
        !manager_dependencies_ready(manager, s))
        return;
    pid_t parent = getpid();
    int pair[2] = {-1, -1};
    if (s->notify || s->watchdog_ms) {
        int enabled = 1;
        if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair) ||
            setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled))) {
            if (pair[0] >= 0)
                close(pair[0]);
            if (pair[1] >= 0)
                close(pair[1]);
            s->next_ms = service_now_ms() + 1000;
            leon_boot_error("notification socket");
            return;
        }
    }
    pid_t pid = fork();
    if (!pid) {
        if (pair[0] >= 0)
            close(pair[0]);
        setsid();
        sigset_t mask;
        sigemptyset(&mask);
        sigprocmask(SIG_SETMASK, &mask, NULL);
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.log", manager->log_directory, s->name);
        int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0640);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2)
                close(fd);
        }
        if (s->uid || s->gid) {
            if (setgroups(0, NULL) || setgid(s->gid) || setuid(s->uid)) {
                perror("service identity");
                _exit(126);
            }
        }
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(126);
        setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
        if (s->uid == 1000) {
            setenv("HOME", "/data/home/leon", 1);
            setenv("XDG_CONFIG_HOME", "/data/home/leon/.config", 1);
            setenv("XDG_STATE_HOME", "/data/home/leon/.local/state", 1);
        }
        setenv("WAYLAND_DISPLAY", "/run/leon/wayland/wayland-0", 1);
        setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket", 1);
        unsetenv("LEON_NOTIFY_FD");
        unsetenv("LEON_WATCHDOG_MS");
        if (pair[1] >= 0) {
            int notify = fcntl(pair[1], F_DUPFD, 32);
            if (notify < 0)
                _exit(126);
            close(pair[1]);
            char value[32];
            snprintf(value, sizeof(value), "%d", notify);
            setenv("LEON_NOTIFY_FD", value, 1);
            snprintf(value, sizeof(value), "%u", s->watchdog_ms);
            setenv("LEON_WATCHDOG_MS", value, 1);
        }
        execv(s->argv[0], s->argv);
        perror(s->argv[0]);
        _exit(127);
    }
    if (pair[1] >= 0)
        close(pair[1]);
    if (pid < 0) {
        if (pair[0] >= 0)
            close(pair[0]);
        s->next_ms = service_now_ms() + 1000;
        leon_boot_error("fork");
        return;
    }
    s->pid = pid;
    s->notify_fd = pair[0];
    s->terminating = s->forced_exit = 0;
    s->state = s->notify ? STARTING : RUNNING;
    s->started_ms = service_now_ms();
    s->ready_deadline = s->started_ms + s->ready_timeout_ms;
    s->watchdog_deadline = s->started_ms + s->watchdog_ms;
    strcpy(s->health, s->notify ? "starting" : "ready");
    leon_boot_log("leond: started %s pid=%d\n", s->name, pid);
}

void manager_reap(struct manager *manager) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < manager->count; ++i) {
            struct service *s = &manager->services[i];
            if (s->pid != pid)
                continue;
            /* A service owns its session; remove descendants before restarting. */
            kill(-pid, SIGKILL);
            if (s->notify_fd >= 0)
                close(s->notify_fd);
            s->notify_fd = -1;
            s->pid = 0;
            s->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            if (s->forced_exit)
                s->exit_status = s->forced_exit;
            s->state = s->exit_status ? FAILED : s->oneshot ? FINISHED : STOPPED;
            int retry = s->restart == 2 || (s->restart == 1 && s->exit_status);
            if (service_now_ms() - s->started_ms > 30000)
                s->retries = 0;
            if (!manager->shutting_down && s->wanted && retry &&
                (unsigned)s->retries < s->restart_limit) {
                ++s->retries;
                s->next_ms = service_now_ms() + 500 * (1 << (s->retries > 6 ? 6 : s->retries));
            } else
                s->wanted = 0;
            leon_boot_log("leond: exited %s status=%d retry=%d\n", s->name, s->exit_status,
                          s->wanted);
        }
    }
}

void manager_stop(struct service *s) {
    s->wanted = 0;
    if (s->pid) {
        s->terminating = 1;
        strcpy(s->health, "stopping");
        kill(-s->pid, SIGTERM);
        s->next_ms = service_now_ms() + 3000;
    } else
        s->state = STOPPED;
}
