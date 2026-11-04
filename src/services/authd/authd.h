#pragma once
#include "auth.h"
#include "credentials.h"
#include "daemon.h"
#include <pthread.h>
#include <sys/socket.h>
#define AUTH_CLIENTS 16
enum auth_role { AUTH_NONE, AUTH_ROOT, AUTH_SHELL, AUTH_SETTINGS, AUTH_COMPOSITOR, AUTH_POWER };
struct auth_policy {
    char kind[16], boot[65];
    struct credential_hash hash;
    int64_t generation, failures, deadline, penalty;
};
struct auth_client {
    int fd, role, watching, pending;
    struct ucred peer;
    uint64_t sequence;
    char request[LEON_AUTH_LIMIT], reply[LEON_AUTH_LIMIT];
    size_t received, sent, size;
    int64_t deadline;
};
struct auth_grant {
    char token[65], epoch[33], purpose[16], action[65];
    pid_t pid;
    uid_t uid;
    int64_t deadline, generation;
};
struct auth_job {
    int active, ready, stop, client, changing, need_old, result, new_result;
    uint64_t sequence;
    char secret[129], replacement[129], kind[16], epoch[33], purpose[16], action[65];
    int64_t generation;
    struct credential_hash old_hash, new_hash;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t thread;
    int event;
};
struct auth_daemon {
    const char *directory, *socket;
    char boot[65], epoch[33];
    int test, locked, valid, watch, singleton;
    struct auth_policy policy;
    struct auth_client clients[AUTH_CLIENTS];
    struct auth_grant grants[16];
    struct auth_job job;
    uint64_t sequence;
    int64_t heartbeat;
};
int64_t auth_now(void);
int auth_policy_open(struct auth_daemon *);
int auth_policy_load(struct auth_daemon *);
int auth_policy_save(struct auth_daemon *);
int auth_secret_valid(const char *kind, const char *secret);
void auth_reply(struct auth_daemon *, int, lm_json *);
void auth_close(struct auth_daemon *, int);
void auth_set_locked(struct auth_daemon *);
lm_json *auth_status(struct auth_daemon *);
void auth_dispatch(struct auth_daemon *, int);
void auth_complete(struct auth_daemon *);
void auth_tick(struct auth_daemon *);
int auth_worker_start(struct auth_daemon *);
void auth_worker_stop(struct auth_daemon *);
int auth_server(struct auth_daemon *);
