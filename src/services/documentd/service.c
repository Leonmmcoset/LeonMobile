#include "document.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void document_revoke(struct documents *s, struct document *p) {
    char token[33];
    snprintf(token, sizeof(token), "%s", p->token);
    if (p->fd >= 0)
        close(p->fd);
    memset(p, 0, sizeof(*p));
    for (size_t i = 0; i < DOCUMENT_LIMIT; ++i)
        if (s->items[i].token[0] && !strcmp(s->items[i].parent, token))
            document_revoke(s, &s->items[i]);
}
void document_expire(struct lm_daemon *d) {
    struct documents *s = d->data;
    for (size_t i = 0; i < DOCUMENT_LIMIT; ++i) {
        struct document *p = &s->items[i];
        if (p->token[0] && (lm_now_ms() >= p->expires ||
            !lm_identity_matches(d, p->uid, p->app, p->installation)))
            document_revoke(s, p);
    }
}
static struct document *allocate(struct lm_daemon *d, uid_t uid, unsigned seconds) {
    struct documents *s = d->data;
    size_t count = 0, slot = DOCUMENT_LIMIT, bytes = 0;
    for (size_t i = 0; i < DOCUMENT_LIMIT; ++i) {
        if (!s->items[i].token[0])
            slot = i;
        else {
            count += s->items[i].uid == uid;
            bytes += s->items[i].bytes;
        }
    }
    if (count >= 4 || slot == DOCUMENT_LIMIT || bytes > 48 * 1024 * 1024)
        return NULL;
    lm_json *app = lm_app_identity(d->options.root, d->options.builtin, uid);
    if (!app)
        return NULL;
    struct document *p = &s->items[slot];
    memset(p, 0, sizeof(*p));
    p->fd = -1;
    p->uid = uid;
    p->expires = lm_now_ms() + seconds * 1000LL;
    snprintf(p->app, sizeof(p->app), "%s", lm_string(lm_get(app, "id"), ""));
    snprintf(p->installation, sizeof(p->installation), "%s",
             lm_string(lm_get(app, "installation"), ""));
    lm_free(app);
    if (lm_token(p->token)) {
        memset(p, 0, sizeof(*p));
        return NULL;
    }
    return p;
}
static lm_json *describe(const struct document *p) {
    lm_json *out = lm_success();
    lm_set_string(out, "token", p->token);
    lm_set_string(out, "app", p->app);
    lm_set_string(out, "name", p->name);
    lm_set_string(out, "state", p->state == 0 ? "choosing" : p->state == 1 ? "ready" : "canceled");
    lm_set_boolean(out, "done", p->state != 0);
    lm_set_integer(out, "bytes", (int64_t)p->bytes);
    lm_set_integer(out, "remaining_ms", p->expires - lm_now_ms());
    return out;
}
lm_json *document_handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    document_expire(d);
    struct documents *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "files"))
        return lm_shared_files(d, uid, r);
    if (!strcmp(op, "pending") || !strcmp(op, "list")) {
        int pending = !strcmp(op, "pending");
        if (pending && !lm_trusted(uid))
            return lm_failure("permission denied");
        lm_json *out = lm_success(), *items = lm_array();
        for (size_t i = 0; i < DOCUMENT_LIMIT; ++i)
            if (s->items[i].token[0] && (lm_trusted(uid) || s->items[i].uid == uid) &&
                (!pending || s->items[i].state == 0))
                lm_append(items, describe(&s->items[i]));
        lm_set(out, pending ? "requests" : "documents", items);
        return out;
    }
    if (!strcmp(op, "reset_app")) {
        if (uid)
            return lm_failure("permission denied");
        const char *id = lm_string(lm_get(r, "app"), "");
        for (size_t i = 0; i < DOCUMENT_LIMIT; ++i)
            if (s->items[i].token[0] && !strcmp(s->items[i].app, id))
                document_revoke(s, &s->items[i]);
        return lm_success();
    }
    if (!strcmp(op, "choose") || !strcmp(op, "offer")) {
        lm_json *app = lm_app_identity(d->options.root, d->options.builtin, uid);
        int choose = !strcmp(op, "choose");
        if (!app || (choose && !lm_declares(app, "files.import"))) {
            lm_free(app);
            return lm_failure("unknown application or files.import not declared");
        }
        lm_free(app);
        int64_t seconds = lm_integer(lm_get(r, "seconds"), 300);
        if (seconds < 1 || seconds > 600 || (lm_get(r, "seconds") &&
            lm_integer(lm_get(r, "seconds"), -1) < 0))
            return lm_failure("seconds must be an integer in 1..600");
        struct document *p = allocate(d, uid, (unsigned)seconds);
        if (!p)
            return lm_failure("document quota or identity unavailable");
        if (!choose) {
            char home[PATH_MAX];
            const char *file = lm_string(lm_get(r, "file"), "");
            int n = snprintf(home, sizeof(home), "%s/data/%s", d->options.root, p->app);
            p->fd = n < 0 || n >= (int)sizeof(home) ? -1 : document_snapshot(home, file, &p->bytes);
            if (p->fd < 0) {
                document_revoke(s, p);
                return lm_failure("private file unavailable, unsafe or exceeds 16 MiB");
            }
            snprintf(p->name, sizeof(p->name), "%s", file);
            p->state = 1;
        }
        return describe(p);
    }
    const char *token = lm_string(lm_get(r, "token"), "");
    struct document *p = NULL;
    for (size_t i = 0; i < DOCUMENT_LIMIT; ++i)
        if (*token && !strcmp(s->items[i].token, token))
            p = &s->items[i];
    if (!p)
        return lm_failure("unknown or expired document");
    if (uid != p->uid && !lm_trusted(uid))
        return lm_failure("permission denied");
    if (!strcmp(op, "selection"))
        return describe(p);
    if (!strcmp(op, "revoke")) {
        document_revoke(s, p);
        return lm_success();
    }
    if (!strcmp(op, "cancel")) {
        if (p->state)
            return lm_failure("completed documents must be revoked");
        if (p->fd >= 0)
            close(p->fd);
        p->fd = -1;
        p->state = 2;
        return describe(p);
    }
    if (!strcmp(op, "pick")) {
        if (!lm_trusted(uid) || p->state)
            return lm_failure("selection unauthorized or already completed");
        size_t total = 0;
        for (size_t i = 0; i < DOCUMENT_LIMIT; ++i)
            if (s->items[i].token[0]) total += s->items[i].bytes;
        if (total > 48 * 1024 * 1024)
            return lm_failure("document memory quota reached");
        char shared[PATH_MAX];
        const char *file = lm_string(lm_get(r, "file"), "");
        int n = snprintf(shared, sizeof(shared), "%s/shared", d->options.root);
        p->fd = n < 0 || n >= (int)sizeof(shared) ? -1 : document_snapshot(shared, file, &p->bytes);
        if (p->fd < 0)
            return lm_failure("selected file unavailable, unsafe or exceeds 16 MiB");
        snprintf(p->name, sizeof(p->name), "%s", file);
        p->state = 1;
        return describe(p);
    }
    if (p->state != 1)
        return lm_failure("document is not ready");
    if (!strcmp(op, "read")) {
        int64_t offset = lm_integer(lm_get(r, "offset"), -1);
        int64_t length = lm_integer(lm_get(r, "length"), -1);
        if (offset < 0 || (uint64_t)offset > p->bytes || length < 1 || length > 2048)
            return lm_failure("invalid offset or length (maximum 2048)");
        char data[2048], hex[4097];
        ssize_t n = pread(p->fd, data, (size_t)length, (off_t)offset);
        if (n < 0)
            return lm_failure("document read failed");
        lm_hex_encode(data, (size_t)n, hex);
        lm_json *out = lm_success();
        lm_set_string(out, "hex", hex);
        lm_set_integer(out, "offset", offset + n);
        lm_set_boolean(out, "eof", (uint64_t)(offset + n) >= p->bytes);
        return out;
    }
    if (!strcmp(op, "delegate")) {
        int64_t from = lm_integer(lm_get(r, "from_uid"), -1);
        int64_t to = lm_integer(lm_get(r, "to_uid"), -1);
        if (uid || from != p->uid || to < 11000 || to >= 60000)
            return lm_failure("permission denied");
        struct document *next = allocate(d, (uid_t)to, 300);
        if (!next)
            return lm_failure("recipient document quota or identity unavailable");
        next->fd = fcntl(p->fd, F_DUPFD_CLOEXEC, 3);
        if (next->fd < 0) {
            document_revoke(s, next);
            return lm_failure("cannot delegate document");
        }
        next->bytes = p->bytes;
        next->expires = p->expires;
        next->state = 1;
        snprintf(next->parent, sizeof(next->parent), "%s", p->token);
        snprintf(next->name, sizeof(next->name), "%s", p->name);
        return describe(next);
    }
    return lm_failure("unknown operation");
}
