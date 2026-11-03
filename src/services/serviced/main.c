#include "manager.h"
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
#include <unistd.h>
int main(int argc, char **argv) {
    struct manager state = {.log_directory = "/var/log/leon"};
    struct manager *manager = &state;
    const char *dir = "/etc/leon/services", *socket_path = "/run/leon/services.sock";
    int check = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check"))
            check = 1;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc)
            dir = argv[++i];
        else if (!strcmp(argv[i], "--socket") && i + 1 < argc)
            socket_path = argv[++i];
        else if (!strcmp(argv[i], "--log-dir") && i + 1 < argc)
            manager->log_directory = argv[++i];
        else {
            fputs("usage: leond [--check] [--config DIR] [--socket PATH]\n", stderr);
            return 2;
        }
    }
    if (manager_load(manager, dir))
        return 1;
    if (check) {
        printf("%d services: dependency graph valid\n", manager->count);
        return 0;
    }
    if (geteuid()) {
        fputs("leond requires root\n", stderr);
        return 1;
    }
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    mkdir(manager->log_directory, 0750);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    signal(SIGPIPE, SIG_IGN);
    int signals = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(socket_path) >= sizeof(address.sun_path))
        return 1;
    strcpy(address.sun_path, socket_path);
    unlink(socket_path);
    mode_t old = umask(0117);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) || listen(server, 16)) {
        perror(socket_path);
        return 1;
    }
    umask(old);
    chmod(socket_path, 0666);
    for (int i = 0; i < manager->count; ++i)
        manager->services[i].wanted = manager->services[i].autostart;
    while (!manager->shutting_down) {
        manager_tick(manager);
        struct pollfd fds[] = {{server, POLLIN, 0}, {signals, POLLIN, 0}};
        poll(fds, 2, 100);
        if (fds[0].revents & POLLIN)
            manager_handle(manager, server);
        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo info;
            while (read(signals, &info, sizeof(info)) == sizeof(info))
                if (info.ssi_signo != SIGCHLD)
                    manager->shutting_down = 1;
        }
    }
    manager_shutdown(manager);
    unlink(socket_path);
    close(server);
    close(signals);
    return 0;
}
