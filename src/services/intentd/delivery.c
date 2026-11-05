#include "intent.h"
#include <stdio.h>
#include <string.h>
static lm_json *call(struct lm_daemon *d, const char *name, lm_json *r) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.sock", d->options.test ? d->options.root : "/run/leon", name);
    lm_json *out = lm_rpc_bounded(path, r, 350);
    lm_free(r);
    return out;
}
void intent_remove(struct lm_daemon *d, struct intent *p) {
    if (p->document[0]) {
        lm_json *r = lm_object();
        lm_set_string(r, "op", "revoke");
        lm_set_string(r, "token", p->document);
        lm_json *out = call(d, d->options.test ? "documentd" : "documents", r);
        lm_free(out);
    }
    lm_free(p->payload);
    memset(p, 0, sizeof(*p));
}
lm_json *intent_description(struct lm_daemon *d, const struct intent *p, int private) {
    lm_json *out = lm_success();
    lm_set_string(out, "token", p->token);
    lm_set_string(out, "source", p->source);
    lm_set_string(out, "target", p->target);
    lm_set_string(out, "state", p->phase == 0 ? "choosing" : p->phase == 1 ? "queued" :
                  p->phase == 2 ? "launching" : p->phase == 3 ? "delivered" : "failed");
    lm_set_string(out, "error", p->error);
    lm_set_boolean(out, "acknowledged", p->acknowledged);
    lm_set_boolean(out, "focused", p->focused);
    if (private) {
        lm_set(out, "payload", lm_copy(p->payload));
        if (!p->phase)
            lm_set(out, "candidates", intent_candidates(d, p->payload));
        struct intents *s = d->data;
        const lm_json *default_app = lm_get(s->defaults, p->key);
        const char *id = lm_string(lm_get(default_app, "id"), "");
        lm_json *app = lm_app_by_id(d->options.root, d->options.builtin, id);
        uid_t uid = (uid_t)lm_integer(lm_get(app, "uid"), 0);
        if (app && intent_matches(app, p->payload) &&
            lm_identity_matches(d, uid, id, lm_string(lm_get(default_app, "installation"), "")))
            lm_set_string(out, "default", id);
        lm_free(app);
    }
    return out;
}
void intent_tick(struct lm_daemon *d) {
    struct intents *s = d->data;
    for (size_t i = 0; i < 32; ++i) {
        struct intent *p = &s->items[i];
        if (!p->token[0])
            continue;
        if (lm_now_ms() >= p->expires ||
            !lm_identity_matches(d, p->uid, p->source, p->installation)) {
            intent_remove(d, p);
            continue;
        }
        if (p->phase < 1 || p->phase > 3)
            continue;
        if (!lm_identity_matches(d, p->target_uid, p->target, p->target_instance)) {
            snprintf(p->error, sizeof(p->error), "recipient installation changed");
            p->phase = 4;
        } else if (p->phase == 1) {
            const char *token = lm_string(lm_get(p->payload, "document"), "");
            if (*token) {
                lm_json *r = lm_object();
                lm_set_string(r, "op", "delegate");
                lm_set_string(r, "token", token);
                lm_set_integer(r, "from_uid", p->uid);
                lm_set_integer(r, "to_uid", p->target_uid);
                lm_json *reply = call(d, d->options.test ? "documentd" : "documents", r);
                if (!lm_boolean(lm_get(reply, "ok"), 0)) {
                    snprintf(p->error, sizeof(p->error), "document delegation failed");
                    p->phase = 4;
                } else {
                    snprintf(p->document, sizeof(p->document), "%s", lm_string(lm_get(reply, "token"), ""));
                    lm_set_string(p->payload, "document", p->document);
                    p->phase = 2;
                }
                lm_free(reply);
            } else
                p->phase = 2;
            return;
        } else if (p->phase == 2) {
            lm_json *r = lm_object();
            lm_set_string(r, "op", "launch");
            lm_set_string(r, "id", p->target);
            lm_json *reply = call(d, "apps", r);
            if (!lm_boolean(lm_get(reply, "ok"), 0)) {
                snprintf(p->error, sizeof(p->error), "application launch failed");
                p->phase = 4;
            } else
                p->phase = 3;
            lm_free(reply);
            return;
        }
    }
}
