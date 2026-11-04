#include "manager.h"
#include "policy.h"
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
void manager_handle(struct manager *manager, int server) {
    int fd = accept4(server, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0)
        return;
    struct ucred peer;
    socklen_t len = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &len)) {
        close(fd);
        return;
    }
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, 100) <= 0) {
        close(fd);
        return;
    }
    char buffer[256] = {0}, verb[32] = {0}, name[64] = {0};
    ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        close(fd);
        return;
    }
    sscanf(buffer, "%31s %63s", verb, name);
    if (!strcmp(verb, "status")) {
        for (int i = 0; i < manager->count; ++i) {
            struct service *s = &manager->services[i];
            if (*name && strcmp(s->name, name))
                continue;
            dprintf(
                fd, "%s\t%s\tpid=%d\texit=%d\tretries=%d\thealth=%s\n", s->name,
                (const char *[]){"stopped", "running", "finished", "failed", "starting"}[s->state],
                s->pid, s->exit_status, s->retries, *s->health ? s->health : "inactive");
        }
    } else if (peer.uid != 0)
        dprintf(fd, "ERR permission denied\n");
    else if (!strcmp(verb, "poweroff") || !strcmp(verb, "reboot")) {
        if (getppid() == 1) {
            kill(1, !strcmp(verb, "reboot") ? SIGUSR2 : SIGUSR1);
            dprintf(fd, "OK\n");
        } else
            dprintf(fd, "ERR not managed by init\n");
    } else {
        int i = manager_index(manager, name);
        if (i < 0)
            dprintf(fd, "ERR unknown service\n");
        else if (!strcmp(verb, "reload")) {
            if (service_policy_apply(manager->policy_directory, &manager->services[i]))
                dprintf(fd, "ERR invalid developer policy\n");
            else
                dprintf(fd, "OK\n");
        } else if (!strcmp(verb, "stop")) {
            manager_stop(&manager->services[i]);
            dprintf(fd, "OK\n");
        } else if (!strcmp(verb, "start") || !strcmp(verb, "restart")) {
            struct service *s = &manager->services[i];
            if (!strcmp(verb, "restart") && s->pid) {
                kill(-s->pid, SIGKILL);
            }
            s->wanted = 1;
            s->retries = 0;
            s->next_ms = 0;
            dprintf(fd, "OK\n");
        } else
            dprintf(fd, "ERR unknown operation\n");
    }
    close(fd);
}
