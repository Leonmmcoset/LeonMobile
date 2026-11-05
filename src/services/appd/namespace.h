#ifndef LEON_APP_NAMESPACE_H
#define LEON_APP_NAMESPACE_H
#include <stddef.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Production launches require all namespaces. There is no fallback. */
pid_t leon_namespace_clone(void);
int leon_namespace_filesystem(const char *home, const char *runtime, const char *package, uid_t uid);
/* Returns only in the application (0) or on setup failure (-1). PID 1 stays
 * in a bounded, unprivileged reaper until the broker closes its channel. */
int leon_namespace_fork(int control, int exec_ack, uid_t uid);
int leon_namespace_close_fds(int first, const int *keep, size_t count);
enum leon_namespace_message { LEON_NS_STARTED = 1, LEON_NS_EXITED, LEON_NS_SIGNAL };
int leon_namespace_identify(int control);
int leon_namespace_signal(int control, int signal);
/* 1 = authenticated message, 0 = would block, -1 = closed/invalid channel.
 * Kernel SCM_CREDENTIALS translates the sender PID into the broker namespace.
 * expected_pid == 0 is used only for the initial, pre-exec identity message. */
int leon_namespace_receive(int control, int kind, pid_t expected_pid, uid_t uid,
                           pid_t *sender, int *status);
#ifdef __cplusplus
}
#endif
#endif
