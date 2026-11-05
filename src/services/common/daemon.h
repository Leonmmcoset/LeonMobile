#ifndef LEON_SYSTEM_DAEMON_H
#define LEON_SYSTEM_DAEMON_H
#include "json.h"
#include <limits.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LM_REQUEST_LIMIT 8191
struct lm_options {
    const char *socket_path, *state, *root, *builtin, *proc, *sys;
    int test;
};
struct lm_daemon {
    struct lm_options options;
    void *data;
    lm_json *(*handle)(struct lm_daemon *, uid_t, const lm_json *);
    void (*tick)(struct lm_daemon *);
};
long long lm_now_ms(void);
int lm_serve(struct lm_daemon *daemon);
int lm_options(int argc, char **argv, struct lm_options *options);
char *lm_dispatch(struct lm_daemon *daemon, uid_t uid, const char *text, size_t size);
char *lm_error(const char *message);
lm_json *lm_failure(const char *message);
lm_json *lm_success(void);
int lm_trusted(uid_t uid);
int lm_mkdir(const char *path, mode_t mode);
lm_json *lm_read_json(const char *path);
int lm_write_json(const char *path, const lm_json *json);
int lm_write_text(const char *path, const char *text);
int lm_read_text(const char *path, char *out, size_t size);
int lm_child_setup(pid_t parent);
int lm_child(char *const argv[], const char *input, unsigned timeout_ms);
lm_json *lm_rpc(const char *socket, const lm_json *request);
lm_json *lm_rpc_bounded(const char *socket, const lm_json *request, unsigned timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
