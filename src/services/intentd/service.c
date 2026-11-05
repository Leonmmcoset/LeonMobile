#include "intent.h"
#include <stdio.h>
#include <string.h>
static int remember(struct lm_daemon *d, const char *key, const lm_json *app) {
    struct intents *s = d->data;
    if (!lm_get(s->defaults, key) && lm_size(s->defaults) >= 64)
        return -1;
    lm_json *next = lm_copy(s->defaults), *entry = lm_object();
    lm_set_string(entry, "id", lm_string(lm_get(app, "id"), ""));
    lm_set_string(entry, "installation", lm_string(lm_get(app, "installation"), ""));
    lm_set(next, key, entry);
    if (lm_write_json(d->options.state, next)) {
        lm_free(next);
        return -1;
    }
    lm_free(s->defaults);
    s->defaults = next;
    return 0;
}
lm_json *intent_handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct intents *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "defaults") || !strcmp(op, "clear_default")) {
        if (!lm_trusted(uid) && uid != 11000)
            return lm_failure("permission denied");
        if (!strcmp(op, "clear_default")) {
            const char *key = lm_string(lm_get(r, "key"), "");
            if (!*key || strlen(key) > 159)
                return lm_failure("invalid default key");
            lm_json *next = lm_copy(s->defaults);
            lm_erase(next, key);
            if (lm_write_json(d->options.state, next)) {
                lm_free(next);
                return lm_failure("cannot persist default removal");
            }
            lm_free(s->defaults);
            s->defaults = next;
        }
        lm_json *out = lm_success();
        lm_set(out, "defaults", lm_copy(s->defaults));
        return out;
    }
    if (!strcmp(op, "reset_app")) {
        if (uid)
            return lm_failure("permission denied");
        const char *id = lm_string(lm_get(r, "app"), "");
        for (size_t i = 0; i < 32; ++i)
            if (s->items[i].token[0] && (!strcmp(s->items[i].source, id) || !strcmp(s->items[i].target, id)))
                intent_remove(d, &s->items[i]);
        return lm_success();
    }
    if (!strcmp(op, "pending") || !strcmp(op, "receive")) {
        int pending = !strcmp(op, "pending");
        if (pending && !lm_trusted(uid))
            return lm_failure("permission denied");
        lm_json *app = lm_app_identity(d->options.root, d->options.builtin, uid);
        if (!pending && !app)
            return lm_failure("unknown application identity");
        lm_free(app);
        lm_json *out = lm_success(), *items = lm_array();
        for (size_t i = 0; i < 32; ++i) {
            struct intent *p = &s->items[i];
            if (!p->token[0] || lm_now_ms() >= p->expires ||
                !lm_identity_matches(d, p->uid, p->source, p->installation))
                continue;
            if (pending ? !p->focused :
                (p->phase == 3 && p->target_uid == uid && !p->acknowledged &&
                 lm_identity_matches(d, uid, p->target, p->target_instance)))
                lm_append(items, intent_description(d, p, 1));
        }
        lm_set(out, pending ? "requests" : "events", items);
        return out;
    }
    if (!strcmp(op, "resolve") || !strcmp(op, "request") || !strcmp(op, "set_default")) {
        char key[160];
        lm_json *payload = intent_validate(r, key);
        if (!payload)
            return lm_failure("invalid action, MIME type, URI or payload");
        if (!strcmp(op, "set_default")) {
            if (!lm_trusted(uid) && uid != 11000) {
                lm_free(payload);
                return lm_failure("permission denied");
            }
            lm_json *entry = lm_app_by_id(d->options.root, d->options.builtin,
                                          lm_string(lm_get(r, "target"), ""));
            uid_t target_uid = (uid_t)lm_integer(lm_get(entry, "uid"), 0);
            lm_json *app = lm_app_identity(d->options.root, d->options.builtin, target_uid);
            int result = !app || !intent_matches(app, payload) ? -1 : remember(d, key, app);
            lm_free(entry); lm_free(app); lm_free(payload);
            return result ? lm_failure("invalid recipient or default persistence failed") : lm_success();
        }
        lm_json *candidates = intent_candidates(d, payload);
        if (!strcmp(op, "resolve")) {
            lm_json *out = lm_success();
            lm_set(out, "candidates", candidates);
            lm_set_string(out, "key", key);
            lm_free(payload);
            return out;
        }
        int available = lm_size(candidates) != 0;
        lm_free(candidates);
        lm_json *app = lm_app_identity(d->options.root, d->options.builtin, uid);
        if (!app || !available) {
            lm_free(app); lm_free(payload);
            return lm_failure(!available ? "no installed handler" : "unknown application identity");
        }
        size_t slot = 32, count = 0;
        for (size_t i = 0; i < 32; ++i)
            if (!s->items[i].token[0]) slot = i;
            else count += s->items[i].uid == uid;
        if (slot == 32 || count >= 4) {
            lm_free(app); lm_free(payload);
            return lm_failure("interaction quota reached");
        }
        struct intent *p = &s->items[slot];
        memset(p, 0, sizeof(*p));
        if (lm_token(p->token)) {
            lm_free(app); lm_free(payload);
            return lm_failure("random source unavailable");
        }
        p->payload = payload;
        p->uid = uid;
        p->expires = lm_now_ms() + 120000;
        snprintf(p->key, sizeof(p->key), "%s", key);
        snprintf(p->source, sizeof(p->source), "%s", lm_string(lm_get(app, "id"), ""));
        snprintf(p->installation, sizeof(p->installation), "%s", lm_string(lm_get(app, "installation"), ""));
        lm_free(app);
        return intent_description(d, p, 0);
    }
    const char *token = lm_string(lm_get(r, "token"), "");
    struct intent *p = NULL;
    for (size_t i = 0; i < 32; ++i)
        if (*token && !strcmp(s->items[i].token, token)) p = &s->items[i];
    if (!p || lm_now_ms() >= p->expires ||
        !lm_identity_matches(d, p->uid, p->source, p->installation))
        return lm_failure("unknown or expired interaction");
    if (!strcmp(op, "ack")) {
        if (p->phase != 3 || p->target_uid != uid ||
            !lm_identity_matches(d, uid, p->target, p->target_instance))
            return lm_failure("permission denied");
        p->acknowledged = 1;
        return lm_success();
    }
    if (uid != p->uid && !lm_trusted(uid))
        return lm_failure("permission denied");
    if (!strcmp(op, "result"))
        return intent_description(d, p, 0);
    if (!strcmp(op, "cancel")) {
        intent_remove(d, p);
        return lm_success();
    }
    if (!lm_trusted(uid))
        return lm_failure("permission denied");
    if (!strcmp(op, "focused")) {
        if (p->phase != 3)
            return lm_failure("interaction has not been delivered");
        p->focused = 1;
        return lm_success();
    }
    if (!strcmp(op, "choose")) {
        const char *target = lm_string(lm_get(r, "target"), "");
        int always = lm_boolean(lm_get(r, "always"), 0);
        if (p->phase || (lm_get(r, "always") && lm_boolean(lm_get(r, "always"), -1) < 0))
            return lm_failure("already selected or invalid always flag");
        lm_json *entry = lm_app_by_id(d->options.root, d->options.builtin, target);
        uid_t target_uid = (uid_t)lm_integer(lm_get(entry, "uid"), 0);
        lm_json *app = lm_app_identity(d->options.root, d->options.builtin, target_uid);
        lm_free(entry);
        if (!app || !intent_matches(app, p->payload)) {
            lm_free(app);
            return lm_failure("recipient does not handle this interaction");
        }
        if (always && remember(d, p->key, app)) {
            lm_free(app);
            return lm_failure("cannot persist default application");
        }
        snprintf(p->target, sizeof(p->target), "%s", target);
        snprintf(p->target_instance, sizeof(p->target_instance), "%s", lm_string(lm_get(app, "installation"), ""));
        p->target_uid = target_uid;
        p->phase = 1;
        lm_free(app);
        return intent_description(d, p, 0);
    }
    return lm_failure("unknown operation");
}
