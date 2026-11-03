#pragma once
#include <sys/types.h>
#define LM_MAX_SERVICES 64
#define LM_MAX_ARGS 32
#define LM_MAX_DEPS 16
struct service {
    char name[64];
    char *argv[LM_MAX_ARGS + 1];
    char *after[LM_MAX_DEPS + 1];
    uid_t uid;
    gid_t gid;
    int restart;
    unsigned restart_limit;
    int autostart;
    pid_t pid;
    int wanted, state, retries, exit_status;
    long long started_ms, next_ms;
};
int service_load(const char *path, struct service *svc, char *error, size_t size);
void service_free(struct service *svc);
int service_order(struct service *services, size_t count, int *order, char *error, size_t size);
