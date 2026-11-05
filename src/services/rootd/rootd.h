#pragma once
#include "daemon.h"
#include "wire.h"
#include <stdint.h>
#include <sys/socket.h>
struct root_options {
    const char *directory, *socket, *proc, *modules, *services, *service_socket;
    int test;
    const char *app_root, *builtin, *auth_socket;
};
struct root_policy {
    int enabled, present, failures;
    int64_t retry_until, generation;
    char salt[33], hash[65];
};
struct root_session_notice {
    int64_t generation;
};
int root_policy_load(const struct root_options *, struct root_policy *);
int root_policy_save(const struct root_options *, const struct root_policy *);
int root_policy_lock(const struct root_options *);
int root_authenticate(const struct root_options *, struct root_policy *, const char *password);
int root_set_password(struct root_policy *, const char *password);
lm_json *root_policy_status(const struct root_policy *);
int root_session(const struct root_options *, int socket, int notice, struct ucred, const lm_json *,
                 int64_t generation);
void root_session_cleanup(const struct root_options *, pid_t worker);
lm_json *root_management(const struct root_options *, const lm_json *, int authorized);
int root_reply(int fd, lm_json *reply, int descriptor);
