#ifndef LEON_SETTINGSD_H
#define LEON_SETTINGSD_H
#include "daemon.h"
struct settings_store {
    lm_json *values;
    int64_t generation;
};
lm_json *settings_defaults(void);
lm_json *settings_schema(void);
int settings_validate(const lm_json *values, int complete);
int settings_load(struct lm_daemon *d);
int settings_save(struct lm_daemon *d, const lm_json *values, int64_t generation);
lm_json *settings_handle(struct lm_daemon *d, uid_t uid, const lm_json *r);
#endif
