#ifndef LEON_INTENTD_H
#define LEON_INTENTD_H
#include "capability.h"
struct intent {
    char token[33], source[97], installation[33], target[97], target_instance[33];
    char key[160], error[128], document[33];
    uid_t uid, target_uid;
    long long expires, launch_until, launch_retry;
    lm_json *payload;
    int phase, focused, acknowledged;
};
struct intents {
    struct intent items[32];
    lm_json *defaults;
};
lm_json *intent_validate(const lm_json *request, char key[160]);
int intent_matches(const lm_json *app, const lm_json *payload);
lm_json *intent_candidates(struct lm_daemon *d, const lm_json *payload);
lm_json *intent_handle(struct lm_daemon *d, uid_t uid, const lm_json *r);
void intent_tick(struct lm_daemon *d);
void intent_remove(struct lm_daemon *d, struct intent *p);
lm_json *intent_description(struct lm_daemon *d, const struct intent *p, int private);
#endif
