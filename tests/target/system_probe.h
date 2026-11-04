#ifndef LEON_SYSTEM_PROBE_H
#define LEON_SYSTEM_PROBE_H
#include <sys/types.h>
int probe_rpc(const char *service, const char *request, uid_t uid);
const char *probe_reply(void);
long long probe_integer(const char *name);
int probe_has(const char *text);
int probe_sandbox(void);
#endif
