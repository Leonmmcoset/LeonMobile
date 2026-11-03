#include "config.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { STOPPED, RUNNING, FINISHED, FAILED };
static struct service services[LM_MAX_SERVICES];
static int count, order[LM_MAX_SERVICES];
static int shutting_down;
static long long now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000; }
static int index_of(const char *s) { for (int i = 0; i < count; ++i) if (!strcmp(services[i].name, s)) return i; return -1; }

static int load(const char *dir) {
    struct dirent **entries;
    int n = scandir(dir, &entries, NULL, alphasort);
    if (n < 0) { perror(dir); return -1; }
    char error[512]; int result = 0;
    for (int i = 0; i < n; ++i) {
        size_t len = strlen(entries[i]->d_name);
        if (len > 5 && !strcmp(entries[i]->d_name + len - 5, ".toml")) {
            char path[1024]; snprintf(path, sizeof(path), "%s/%s", dir, entries[i]->d_name);
            if (count == LM_MAX_SERVICES || service_load(path, &services[count], error, sizeof(error))) {
                fprintf(stderr, "leond: %s\n", count == LM_MAX_SERVICES ? "too many services" : error); result = -1;
            } else ++count;
        }
        free(entries[i]);
    }
    free(entries);
    if (!result && service_order(services, count, order, error, sizeof(error))) { fprintf(stderr, "leond: %s\n", error); return -1; }
    return result;
}

static int dependencies_ready(struct service *s) {
    for (int i = 0; s->after[i]; ++i) {
        int j = index_of(s->after[i]);
        if (j < 0 || (services[j].oneshot ? services[j].state != FINISHED : services[j].state != RUNNING)) return 0;
    }
    return 1;
}

static void start(struct service *s) {
    if (s->pid || !s->wanted || now_ms() < s->next_ms || !dependencies_ready(s)) return;
    pid_t pid = fork();
    if (!pid) {
        setsid();
        sigset_t mask; sigemptyset(&mask); sigprocmask(SIG_SETMASK, &mask, NULL);
        char path[256]; snprintf(path, sizeof(path), "/var/log/leon/%s.log", s->name);
        int fd = open(path, O_WRONLY|O_APPEND|O_CREAT|O_NOFOLLOW, 0640);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); if (fd > 2) close(fd); }
        if (s->uid || s->gid) {
            if (setgroups(0, NULL) || setgid(s->gid) || setuid(s->uid)) { perror("service identity"); _exit(126); }
        }
        setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
        setenv("WAYLAND_DISPLAY", "/run/leon/wayland/wayland-0", 1);
        setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket", 1);
        execv(s->argv[0], s->argv); perror(s->argv[0]); _exit(127);
    }
    if (pid < 0) { s->next_ms = now_ms() + 1000; perror("fork"); return; }
    s->pid = pid; s->state = RUNNING; s->started_ms = now_ms();
    fprintf(stderr, "leond: started %s pid=%d\n", s->name, pid);
}

static void reap(void) {
    int status; pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < count; ++i) {
            struct service *s = &services[i]; if (s->pid != pid) continue;
            /* A service owns its session; remove descendants before restarting. */
            kill(-pid, SIGKILL);
            s->pid = 0; s->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            s->state = s->exit_status ? FAILED : FINISHED;
            int retry = s->restart == 2 || (s->restart == 1 && s->exit_status);
            if (now_ms() - s->started_ms > 30000) s->retries = 0;
            if (!shutting_down && s->wanted && retry && (unsigned)s->retries < s->restart_limit) {
                ++s->retries; s->next_ms = now_ms() + 500 * (1 << (s->retries > 6 ? 6 : s->retries));
            } else s->wanted = 0;
            fprintf(stderr, "leond: exited %s status=%d retry=%d\n", s->name, s->exit_status, s->wanted);
        }
    }
}

static void stop(struct service *s) { s->wanted = 0; if (s->pid) { kill(-s->pid, SIGTERM); s->next_ms = now_ms() + 3000; } else s->state = STOPPED; }

static void handle(int server) {
    int fd = accept4(server, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK); if (fd < 0) return;
    struct ucred peer; socklen_t len = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &len)) { close(fd); return; }
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, 100) <= 0) { close(fd); return; }
    char buffer[256] = {0}, verb[32] = {0}, name[64] = {0};
    ssize_t n = recv(fd, buffer, sizeof(buffer)-1, 0);
    if (n <= 0) { close(fd); return; }
    sscanf(buffer, "%31s %63s", verb, name);
    if (!strcmp(verb, "status")) {
        for (int i = 0; i < count; ++i) {
            struct service *s = &services[i]; if (*name && strcmp(s->name, name)) continue;
            dprintf(fd, "%s\t%s\tpid=%d\texit=%d\tretries=%d\n", s->name,
                (const char *[]){"stopped", "running", "finished", "failed"}[s->state], s->pid, s->exit_status, s->retries);
        }
    } else if (peer.uid != 0) dprintf(fd, "ERR permission denied\n");
    else if (!strcmp(verb, "poweroff") || !strcmp(verb, "reboot")) {
        if (getppid() == 1) { kill(1, !strcmp(verb, "reboot") ? SIGUSR2 : SIGUSR1); dprintf(fd, "OK\n"); }
        else dprintf(fd, "ERR not managed by init\n");
    } else {
        int i = index_of(name);
        if (i < 0) dprintf(fd, "ERR unknown service\n");
        else if (!strcmp(verb, "stop")) { stop(&services[i]); dprintf(fd, "OK\n"); }
        else if (!strcmp(verb, "start") || !strcmp(verb, "restart")) {
            struct service *s = &services[i];
            if (!strcmp(verb, "restart") && s->pid) { kill(-s->pid, SIGKILL); }
            s->wanted = 1; s->retries = 0; s->next_ms = 0; dprintf(fd, "OK\n");
        } else dprintf(fd, "ERR unknown operation\n");
    }
    close(fd);
}

int main(int argc, char **argv) {
    const char *dir = "/etc/leon/services", *socket_path = "/run/leon/services.sock";
    int check = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--socket") && i + 1 < argc) socket_path = argv[++i];
        else { fputs("usage: leond [--check] [--config DIR] [--socket PATH]\n", stderr); return 2; }
    }
    if (load(dir)) return 1;
    if (check) { printf("%d services: dependency graph valid\n", count); return 0; }
    if (geteuid()) { fputs("leond requires root\n", stderr); return 1; }
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    mkdir("/var/log/leon", 0750);
    sigset_t mask; sigemptyset(&mask); sigaddset(&mask, SIGCHLD); sigaddset(&mask, SIGTERM); sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL); signal(SIGPIPE, SIG_IGN);
    int signals = signalfd(-1, &mask, SFD_CLOEXEC|SFD_NONBLOCK);
    int server = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(socket_path) >= sizeof(address.sun_path)) return 1;
    strcpy(address.sun_path, socket_path); unlink(socket_path);
    mode_t old = umask(0117);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) || listen(server, 16)) { perror(socket_path); return 1; }
    umask(old);
    for (int i = 0; i < count; ++i) services[i].wanted = services[i].autostart;
    while (!shutting_down) {
        reap();
        for (int n = 0; n < count; ++n) {
            struct service *s = &services[order[n]];
            if (!s->wanted && s->pid && now_ms() > s->next_ms) kill(-s->pid, SIGKILL);
            start(s);
        }
        struct pollfd fds[] = {{server, POLLIN, 0}, {signals, POLLIN, 0}};
        poll(fds, 2, 100);
        if (fds[0].revents & POLLIN) handle(server);
        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo info;
            while (read(signals, &info, sizeof(info)) == sizeof(info)) if (info.ssi_signo != SIGCHLD) shutting_down = 1;
        }
    }
    for (int n = count - 1; n >= 0; --n) stop(&services[order[n]]);
    long long deadline = now_ms() + 3000;
    while (now_ms() < deadline) { reap(); int live = 0; for (int i = 0; i < count; ++i) live += services[i].pid > 0; if (!live) break; usleep(10000); }
    for (int i = 0; i < count; ++i) { if (services[i].pid) kill(-services[i].pid, SIGKILL); service_free(&services[i]); }
    unlink(socket_path); close(server); close(signals); return 0;
}
