#ifndef LEON_APP_SANDBOX_H
#define LEON_APP_SANDBOX_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
int leon_sandbox(const char *home, const char *runtime, int network);
int leon_cgroup(pid_t pid, uid_t uid);
int leon_cgroup_kill(uid_t uid);
int leon_cgroup_reset(void);
#ifdef __cplusplus
}
#endif
#endif
