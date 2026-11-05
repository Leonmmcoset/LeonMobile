#pragma once
#include "rootd.h"
#define ROOT_APP_REQUESTS 2
int root_apps_lock(const struct root_options *);
lm_json *root_apps_read(const struct root_options *, const char *name, int *missing);
int root_apps_write(const struct root_options *, const char *name, const lm_json *);
lm_json *root_apps_records(const struct root_options *);
int root_apps_audit(const struct root_options *, const lm_json *, const char *event, int status);
void root_apps_name(char *, size_t, pid_t);
int64_t root_process_start(pid_t);
int root_apps_unlocked(const struct root_options *, char epoch[33]);
int root_apps_current(const struct root_options *, const lm_json *);
int root_apps_revision(const struct root_options *, const lm_json *);
lm_json *root_apps_serve(const struct root_options *, int socket, int notice, struct ucred,
                         const lm_json *, int settings, int shell);
int root_apps_cleanup(const struct root_options *, pid_t);
int root_command(const struct root_options *, int socket, int helper, const lm_json *);
int root_session_slot(const struct root_options *);
int root_session_group(const struct root_options *);
