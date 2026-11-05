#ifndef LEON_NETD_H
#define LEON_NETD_H
#include "daemon.h"
#include <net/if.h>
struct network_state {
    pid_t dhcp, apply;
    int wanted, failures, apply_needed;
    long long next, generation, sample, apply_deadline;
    uint64_t fingerprint;
    char interface[IFNAMSIZ], error[160], policy_path[PATH_MAX];
    lm_json *policy;
};
int netd_run(int argc, char **argv);
lm_json *network_defaults(void);
int network_validate(const lm_json *policy);
int network_load(struct lm_daemon *d);
lm_json *network_status(struct lm_daemon *d);
void network_tick(struct lm_daemon *d);
void network_stop(pid_t *pid);
#endif
