#pragma once
#include "config.h"
enum service_state { STOPPED, RUNNING, FINISHED, FAILED };
struct manager {
    struct service services[LM_MAX_SERVICES];
    int count, order[LM_MAX_SERVICES];
    int shutting_down;
    const char *log_directory;
};
long long service_now_ms(void);
int manager_index(const struct manager *manager, const char *name);
int manager_load(struct manager *manager, const char *directory);
int manager_dependencies_ready(const struct manager *manager, const struct service *service);
void manager_start(struct manager *manager, struct service *service);
void manager_stop(struct service *service);
void manager_reap(struct manager *manager);
void manager_tick(struct manager *manager);
void manager_shutdown(struct manager *manager);
void manager_handle(struct manager *manager, int socket);
