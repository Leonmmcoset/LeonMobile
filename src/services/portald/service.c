#include "service.h"
#include "daemon.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int declared(const lm_json *app, const char *permission) {
    const lm_json *list = lm_get(app, "permissions");
    for (size_t i = 0; i < lm_size(list); ++i)
        if (!strcmp(lm_string(lm_at(list, i), ""), permission))
            return 1;
    return 0;
}
int portal_granted(struct portal *s, const lm_json *app, const char *permission) {
    const char *id = lm_string(lm_get(app, "id"), "");
    return declared(app, permission) && lm_boolean(lm_get(lm_get(s->grants, id), permission), 0);
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct portal *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    for (size_t i = 0; i < 32; ++i)
        if (s->pending[i].token && lm_now_ms() >= s->pending[i].expires)
            s->pending[i].token = 0;
    if (!strcmp(op, "pending")) {
        if (!lm_trusted(uid))
            return lm_failure("permission denied");
        lm_json *out = lm_success(), *list = lm_array();
        for (size_t i = 0; i < 32; ++i)
            if (s->pending[i].token && !s->pending[i].completed) {
                lm_json *item = lm_object();
                lm_set_integer(item, "token", s->pending[i].token);
                lm_set_string(item, "app", s->pending[i].app);
                lm_set_string(item, "permission", s->pending[i].permission);
                lm_set_boolean(item, "file_request", s->pending[i].file_request);
                lm_append(list, item);
            }
        lm_set(out, "requests", list);
        return out;
    }
    if (!strcmp(op, "decide")) {
        if (!lm_trusted(uid))
            return lm_failure("permission denied");
        long long token = lm_integer(lm_get(r, "token"), -1);
        int allow = lm_boolean(lm_get(r, "allow"), -1);
        if (token < 1 || token > INT_MAX || allow < 0)
            return lm_failure("invalid decision");
        for (size_t i = 0; i < 32; ++i)
            if (s->pending[i].token == (unsigned)token) {
                struct request *p = &s->pending[i];
                if (p->file_request) {
                    if (allow)
                        return lm_failure("select a file with pick");
                    p->completed = 1;
                    return lm_success();
                }
                lm_json *app = lm_app(d->options.root, d->options.builtin, p->uid);
                if (!app || !declared(app, p->permission)) {
                    lm_free(app);
                    p->token = 0;
                    return lm_failure("application no longer installed or permission not declared");
                }
                lm_free(app);
                lm_json *next = lm_copy(s->grants), *permissions = lm_copy(lm_get(next, p->app));
                lm_set_boolean(permissions, p->permission, allow);
                lm_set(next, p->app, permissions);
                if (lm_write_json(d->options.state, next)) {
                    lm_free(next);
                    return lm_failure("cannot persist grant");
                }
                lm_free(s->grants);
                s->grants = next;
                p->token = 0;
                return lm_success();
            }
        return lm_failure("request expired or unknown");
    }
    if (!strcmp(op, "query") || !strcmp(op, "request")) {
        uid_t subject = uid;
        if (lm_get(r, "uid")) {
            if (uid)
                return lm_failure("permission denied");
            long long requested = lm_integer(lm_get(r, "uid"), -1);
            if (requested < 0 || requested > 60000)
                return lm_failure("invalid UID");
            subject = (uid_t)requested;
        }
        lm_json *app = lm_app(d->options.root, d->options.builtin, subject);
        if (!app)
            return lm_failure("unknown application identity");
        const char *permission = lm_string(lm_get(r, "permission"), "");
        if (!declared(app, permission)) {
            lm_free(app);
            return lm_failure("permission not declared by installed manifest");
        }
        lm_json *out = lm_success();
        int allowed = portal_granted(s, app, permission);
        lm_set_boolean(out, "granted", allowed);
        if (!allowed && !strcmp(op, "request")) {
            size_t slot = 32;
            for (size_t i = 0; i < 32; ++i)
                if (s->pending[i].token && s->pending[i].uid == subject &&
                    !strcmp(s->pending[i].permission, permission)) {
                    slot = i;
                    break;
                }
            if (slot == 32)
                for (size_t i = 0; i < 32; ++i)
                    if (!s->pending[i].token) {
                        slot = i;
                        break;
                    }
            if (slot == 32) {
                lm_free(app);
                lm_free(out);
                return lm_failure("permission request limit reached");
            }
            struct request *p = &s->pending[slot];
            if (!p->token) {
                unsigned token;
                do {
                    if (getrandom(&token, sizeof(token), 0) != (ssize_t)sizeof(token)) {
                        lm_free(app);
                        lm_free(out);
                        return lm_failure("random source unavailable");
                    }
                    int collision = 0;
                    for (size_t j = 0; j < 32; ++j)
                        if (s->pending[j].token == token)
                            collision = 1;
                    if (token && token <= INT_MAX && !collision)
                        break;
                } while (1);
                *p = (struct request){.token = token};
                p->uid = subject;
                p->expires = lm_now_ms() + 120000;
                snprintf(p->app, sizeof(p->app), "%s", lm_string(lm_get(app, "id"), ""));
                snprintf(p->permission, sizeof(p->permission), "%s", permission);
            }
            lm_set_integer(out, "token", p->token);
        }
        int pending = 0;
        for (size_t i = 0; i < 32; ++i)
            if (s->pending[i].token && !s->pending[i].file_request &&
                s->pending[i].uid == subject && !strcmp(s->pending[i].permission, permission))
                pending = 1;
        lm_set_boolean(out, "pending", pending);
        lm_free(app);
        return out;
    }
    if (!strcmp(op, "choose")) {
        lm_json *app = lm_app(d->options.root, d->options.builtin, uid);
        if (!app || !portal_granted(s, app, "files.import")) {
            lm_free(app);
            return lm_failure("file import is not authorized");
        }
        size_t slot = 32;
        for (size_t i = 0; i < 32; ++i)
            if (s->pending[i].token && s->pending[i].uid == uid && s->pending[i].file_request &&
                !s->pending[i].completed) {
                slot = i;
                break;
            }
        if (slot == 32)
            for (size_t i = 0; i < 32; ++i)
                if (!s->pending[i].token) {
                    slot = i;
                    break;
                }
        if (slot == 32) {
            lm_free(app);
            return lm_failure("selection limit reached");
        }
        struct request *p = &s->pending[slot];
        if (!p->token) {
            unsigned token;
            for (;;) {
                if (getrandom(&token, sizeof(token), 0) != (ssize_t)sizeof(token)) {
                    lm_free(app);
                    return lm_failure("random source unavailable");
                }
                int collision = 0;
                for (size_t i = 0; i < 32; ++i)
                    if (s->pending[i].token == token)
                        collision = 1;
                if (token && token <= INT_MAX && !collision)
                    break;
            }
            *p = (struct request){
                .token = token, .uid = uid, .expires = lm_now_ms() + 120000, .file_request = 1};
            snprintf(p->app, sizeof(p->app), "%s", lm_string(lm_get(app, "id"), ""));
            strcpy(p->permission, "files.import");
        }
        lm_free(app);
        lm_json *out = lm_success();
        lm_set_integer(out, "token", p->token);
        return out;
    }
    if (!strcmp(op, "selection") || !strcmp(op, "pick")) {
        long long token = lm_integer(lm_get(r, "token"), -1);
        if (token < 1 || token > INT_MAX)
            return lm_failure("invalid token");
        for (size_t i = 0; i < 32; ++i)
            if (s->pending[i].token == (unsigned)token && s->pending[i].file_request) {
                struct request *p = &s->pending[i];
                if (!strcmp(op, "selection")) {
                    if (uid != p->uid && !lm_trusted(uid))
                        return lm_failure("permission denied");
                    lm_json *out = lm_success();
                    lm_set_boolean(out, "done", p->completed);
                    lm_set_string(out, "path", p->selected);
                    return out;
                }
                if (!lm_trusted(uid) || p->completed)
                    return lm_failure("selection unauthorized or completed");
                lm_json *import = lm_object();
                lm_set_string(import, "op", "import");
                lm_set_integer(import, "uid", p->uid);
                lm_set_string(import, "file", lm_string(lm_get(r, "file"), ""));
                lm_json *out = handle(d, uid, import);
                lm_free(import);
                if (lm_boolean(lm_get(out, "ok"), 0)) {
                    snprintf(p->selected, sizeof(p->selected), "%s",
                             lm_string(lm_get(out, "path"), ""));
                    p->completed = 1;
                }
                return out;
            }
        return lm_failure("selection expired or unknown");
    }
    if (!strcmp(op, "files"))
        return portal_files(d, uid, r);
    if (!strcmp(op, "revoke")) {
        if (!lm_trusted(uid))
            return lm_failure("permission denied");
        const char *app = lm_string(lm_get(r, "app"), ""),
                   *permission = lm_string(lm_get(r, "permission"), "");
        lm_json *installed = lm_app_by_id(d->options.root, d->options.builtin, app);
        if (!installed || !declared(installed, permission)) {
            lm_free(installed);
            return lm_failure("unknown application or undeclared permission");
        }
        lm_free(installed);
        lm_json *next = lm_copy(s->grants), *permissions = lm_copy(lm_get(next, app));
        lm_set_boolean(permissions, permission, 0);
        lm_set(next, app, permissions);
        if (lm_write_json(d->options.state, next)) {
            lm_free(next);
            return lm_failure("cannot persist revocation");
        }
        lm_free(s->grants);
        s->grants = next;
        if (!d->options.test) {
            lm_json *stop = lm_object();
            lm_set_string(stop, "op", "stop");
            lm_set_string(stop, "id", app);
            lm_json *reply = lm_rpc("/run/leon/apps.sock", stop);
            lm_free(stop);
            int stopped = lm_boolean(lm_get(reply, "ok"), 0);
            lm_free(reply);
            if (!stopped)
                return lm_failure("grant revoked; application stop failed, retry revocation");
        }
        return lm_success();
    }
    if (!strcmp(op, "import"))
        return portal_import(d, uid, r);
    return lm_failure("unknown operation");
}
int portald_run(int argc, char **argv) {
    struct portal s = {0};
    struct lm_daemon d = {.options = {"/run/leon/portal.sock", "/data/leon/system/grants.json",
                                      "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &s,
                          .handle = handle};
    if (lm_options(argc, argv, &d.options))
        return 2;
    s.grants = lm_read_json(d.options.state);
    if (!s.grants) {
        if (access(d.options.state, F_OK) == 0)
            return 1;
        s.grants = lm_object();
    }
    if (!lm_grants_valid(s.grants)) {
        lm_free(s.grants);
        return 1;
    }
    int result = lm_serve(&d);
    lm_free(s.grants);
    return result;
}
