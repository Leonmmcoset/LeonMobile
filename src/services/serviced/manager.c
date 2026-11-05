#include "manager.h"
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
long long service_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}
static int manager_has_live_dependents(const struct manager *manager, const struct service *s) {
    for (int i = 0; i < manager->count; ++i) {
        if (!manager->services[i].pid)
            continue;
        for (int j = 0; manager->services[i].after[j]; ++j)
            if (!strcmp(manager->services[i].after[j], s->name))
                return 1;
    }
    return 0;
}

void manager_tick(struct manager *manager) {
    manager_logs(manager);
    manager_reap(manager);
    for (int n = 0; n < manager->count; ++n) {
        struct service *service = &manager->services[manager->order[n]];
        manager_notifications(service);
        if (service->terminating && service->pid && service_now_ms() > service->next_ms)
            kill(-service->pid, SIGKILL);
        manager_start(manager, service);
    }
}
void manager_shutdown(struct manager *manager) {
    /* A dependency must remain alive while its clients complete shutdown.
     * Independent leaves can stop together, with the usual per-service grace. */
    int stopping[LM_MAX_SERVICES] = {0};
    for (int i = 0; i < manager->count; ++i)
        manager->services[i].wanted = 0;
    long long deadline = service_now_ms() + 30000;
    while (service_now_ms() < deadline) {
        manager_logs(manager);
        manager_reap(manager);
        int live = 0;
        for (int n = manager->count - 1; n >= 0; --n) {
            int i = manager->order[n];
            struct service *s = &manager->services[i];
            if (!s->pid)
                continue;
            ++live;
            if (!stopping[i] && !manager_has_live_dependents(manager, s)) {
                manager_stop(s);
                stopping[i] = 1;
            } else if (stopping[i] && service_now_ms() > s->next_ms)
                kill(-s->pid, SIGKILL);
        }
        if (!live)
            break;
        usleep(10000);
    }
    for (int i = 0; i < manager->count; ++i) {
        if (manager->services[i].pid)
            kill(-manager->services[i].pid, SIGKILL);
        service_free(&manager->services[i]);
    }
}
