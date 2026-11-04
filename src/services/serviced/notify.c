#include "manager.h"
#include <signal.h>
#include <string.h>
#include <sys/socket.h>

static void timeout(struct service *service, const char *reason, int status) {
    service->terminating = 1;
    service->forced_exit = status;
    strcpy(service->health, reason);
    kill(-service->pid, SIGTERM);
    service->next_ms = service_now_ms() + 3000;
}

void manager_notifications(struct service *service) {
    if (!service->pid || service->terminating)
        return;
    for (int i = 0; service->notify_fd >= 0 && i < 8; ++i) {
        char text[32], control[CMSG_SPACE(sizeof(struct ucred))];
        struct iovec data = {text, sizeof(text)};
        struct msghdr message = {.msg_iov = &data,
                                 .msg_iovlen = 1,
                                 .msg_control = control,
                                 .msg_controllen = sizeof(control)};
        ssize_t count = recvmsg(service->notify_fd, &message, MSG_DONTWAIT);
        if (count < 0)
            break;
        struct ucred *peer = NULL;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c))
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS &&
                c->cmsg_len == CMSG_LEN(sizeof(struct ucred)))
                peer = (struct ucred *)CMSG_DATA(c);
        if (!peer || peer->pid != service->pid || peer->uid != service->uid ||
            message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))
            continue;
        if (count == 7 && !memcmp(text, "READY=1", 7) && service->state == STARTING) {
            service->state = RUNNING;
            strcpy(service->health, "ready");
            service->watchdog_deadline = service_now_ms() + service->watchdog_ms;
        } else if (count == 10 && !memcmp(text, "WATCHDOG=1", 10) && service->state == RUNNING)
            service->watchdog_deadline = service_now_ms() + service->watchdog_ms;
    }
    long long now = service_now_ms();
    if (service->state == STARTING && now >= service->ready_deadline)
        timeout(service, "ready-timeout", 124);
    else if (service->state == RUNNING && service->watchdog_ms && now >= service->watchdog_deadline)
        timeout(service, "watchdog-timeout", 125);
}
