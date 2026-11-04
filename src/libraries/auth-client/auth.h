#pragma once
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LEON_AUTH_SOCKET "/run/leon-auth/control.sock"
#define LEON_AUTH_LIMIT 4096
struct auth_state {
    int ok, locked, configured, trusted;
    int64_t generation, retry_after_ms;
    char epoch[33], kind[16], token[65], error[160];
};
int auth_decode(const char *, struct auth_state *);
int auth_exchange(const char *path, const char *request, char *response, unsigned timeout_ms);
int auth_lock(const char *path);
int auth_watch(const char *path);
int auth_verify_peer(const char *path, pid_t pid, uid_t uid, const char *role);
int auth_peer(pid_t pid, uid_t uid, uid_t expected, const char *executable, const char *parent);
#ifdef __cplusplus
}
#endif
