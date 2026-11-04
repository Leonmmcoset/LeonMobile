#include "authd.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
static void *worker(void *data) {
    struct auth_job *j = data;
    pthread_mutex_lock(&j->mutex);
    for (;;) {
        while ((!j->active || j->ready) && !j->stop)
            pthread_cond_wait(&j->wake, &j->mutex);
        if (j->stop)
            break;
        /* Only the KDF runs here. Policy, tokens and socket state stay on the main thread. */
        j->result = j->need_old ? credential_check(&j->old_hash, j->secret) : 0;
        j->new_result = 0;
        if (!j->result && j->changing && strcmp(j->kind, "none"))
            j->new_result = credential_create(&j->new_hash, j->replacement);
        credential_clear(j->secret, sizeof(j->secret));
        credential_clear(j->replacement, sizeof(j->replacement));
        j->ready = 1;
        uint64_t one = 1;
        (void)write(j->event, &one, sizeof(one));
    }
    pthread_mutex_unlock(&j->mutex);
    return NULL;
}
int auth_worker_start(struct auth_daemon *d) {
    struct auth_job *j = &d->job;
    j->event = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (j->event < 0 || pthread_mutex_init(&j->mutex, NULL) || pthread_cond_init(&j->wake, NULL))
        return -1;
    return pthread_create(&j->thread, NULL, worker, j) ? -1 : 0;
}
void auth_worker_stop(struct auth_daemon *d) {
    pthread_mutex_lock(&d->job.mutex);
    d->job.stop = 1;
    pthread_cond_signal(&d->job.wake);
    pthread_mutex_unlock(&d->job.mutex);
    pthread_join(d->job.thread, NULL);
    close(d->job.event);
    pthread_cond_destroy(&d->job.wake);
    pthread_mutex_destroy(&d->job.mutex);
    credential_clear(&d->job, sizeof(d->job));
}
void auth_set_locked(struct auth_daemon *d) {
    d->locked = 1;
    if (credential_random(d->epoch, 16))
        d->valid = 0;
    credential_clear(d->grants, sizeof(d->grants));
    d->heartbeat = 0;
}
lm_json *auth_status(struct auth_daemon *d) {
    lm_json *j = d->valid
                     ? lm_success()
                     : lm_failure("device credential policy is invalid; session remains locked");
    lm_set_boolean(j, "locked", d->locked || !d->valid);
    lm_set_boolean(j, "configured", strcmp(d->policy.kind, "none") != 0);
    lm_set_string(j, "kind", d->valid ? d->policy.kind : "unavailable");
    lm_set_string(j, "epoch", d->epoch);
    lm_set_integer(j, "generation", d->policy.generation);
    int64_t remaining = d->policy.deadline - auth_now();
    lm_set_integer(j, "retry_after_ms", remaining > 0 ? remaining : 0);
    return j;
}
static void clear_fields(lm_json *r) {
    for (const char **key = (const char *[]){"secret", "new_secret", NULL}; *key; ++key) {
        char *text = (char *)lm_string(lm_get(r, *key), "");
        credential_clear(text, strlen(text));
    }
}
static int role(const struct auth_client *c, int r) { return c->role == r || c->role == AUTH_ROOT; }
static int grant_create(struct auth_daemon *d, struct auth_client *c, struct auth_job *j,
                        char out[65]) {
    for (size_t i = 0; i < 16; ++i)
        if (!d->grants[i].token[0] || d->grants[i].deadline <= auth_now()) {
            struct auth_grant *g = &d->grants[i];
            *g = (struct auth_grant){.pid = c->peer.pid,
                                     .uid = c->peer.uid,
                                     .deadline = auth_now() + 15000,
                                     .generation = d->policy.generation};
            strcpy(g->epoch, d->epoch);
            strcpy(g->purpose, j->purpose);
            strcpy(g->action, j->action);
            if (credential_random(g->token, 32))
                return -1;
            strcpy(out, g->token);
            return 0;
        }
    return -1;
}
static lm_json *consume(struct auth_daemon *d, struct auth_client *c, const lm_json *r) {
    const char *token = lm_string(lm_get(r, "token"), ""),
               *purpose = lm_string(lm_get(r, "purpose"), "");
    const char *action = lm_string(lm_get(r, "action"), "");
    if ((!strcmp(purpose, "unlock") && !role(c, AUTH_COMPOSITOR)) ||
        (!strcmp(purpose, "settings") && !role(c, AUTH_SETTINGS)) ||
        (strcmp(purpose, "unlock") && strcmp(purpose, "settings")))
        return lm_failure("unauthorized grant audience");
    for (size_t i = 0; i < 16; ++i) {
        struct auth_grant *g = &d->grants[i];
        if (!*token || strcmp(g->token, token))
            continue;
        int valid =
            g->deadline > auth_now() && g->generation == d->policy.generation &&
            !strcmp(g->epoch, d->epoch) && !strcmp(g->purpose, purpose) &&
            !strcmp(g->action, action) &&
            (!strcmp(purpose, "unlock") || (g->pid == c->peer.pid && g->uid == c->peer.uid));
        if (!valid)
            return lm_failure("grant expired or does not match this request");
        credential_clear(g, sizeof(*g));
        if (!strcmp(purpose, "unlock")) {
            d->locked = 0;
            d->heartbeat = 0;
        }
        return auth_status(d);
    }
    return lm_failure("unknown or already consumed grant");
}
void auth_dispatch(struct auth_daemon *d, int index) {
    struct auth_client *c = &d->clients[index];
    lm_json *r = lm_parse(c->request, strlen(c->request)), *reply = NULL;
    credential_clear(c->request, sizeof(c->request));
    c->received = 0;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!lm_is_object(r)) {
        reply = lm_failure("invalid authentication request");
        goto done;
    }
    if (c->role == AUTH_NONE) {
        reply = lm_failure("untrusted authentication caller");
        goto done;
    }
    if (!strcmp(op, "status")) {
        reply = auth_status(d);
        goto done;
    }
    if (!strcmp(op, "verify_peer")) {
        const char *name = lm_string(lm_get(r, "role"), "");
        int64_t pid = lm_integer(lm_get(r, "pid"), 0), uid = lm_integer(lm_get(r, "uid"), -1);
        if (!role(c, AUTH_COMPOSITOR) || pid < 2 || pid > INT32_MAX || uid != 1000 ||
            (strcmp(name, "shell") && strcmp(name, "ime")))
            reply = lm_failure("invalid peer verification request");
        else {
            reply = lm_success();
            lm_set_boolean(
                reply, "trusted",
                auth_peer((pid_t)pid, (uid_t)uid, 1000,
                          !strcmp(name, "shell") ? "/usr/bin/leon-shell" : "/usr/bin/imed",
                          "/usr/sbin/leond"));
        }
        goto done;
    }
    if (!strcmp(op, "lock")) {
        if (!role(c, AUTH_COMPOSITOR) && !role(c, AUTH_SHELL) && !role(c, AUTH_POWER))
            reply = lm_failure("caller cannot lock the session");
        else {
            auth_set_locked(d);
            reply = auth_status(d);
        }
        goto done;
    }
    if (!strcmp(op, "watch")) {
        if (!role(c, AUTH_COMPOSITOR) || (d->watch >= 0 && d->watch != index))
            reply = lm_failure("session monitor unavailable to this caller");
        else {
            c->watching = 1;
            d->watch = index;
            auth_set_locked(d);
            reply = auth_status(d);
        }
        goto done;
    }
    if (!d->valid) {
        reply = auth_status(d);
        goto done;
    }
    if (!strcmp(op, "consume")) {
        reply = consume(d, c, r);
        goto done;
    }
    int changing = !strcmp(op, "configure");
    const char *purpose = lm_string(lm_get(r, "purpose"), "unlock"),
               *action = lm_string(lm_get(r, "action"), "");
    if (strcmp(op, "authenticate") && !changing) {
        reply = lm_failure("unknown authentication operation");
        goto done;
    }
    if ((changing && !role(c, AUTH_SETTINGS)) ||
        (!changing && ((!strcmp(purpose, "unlock") && !role(c, AUTH_SHELL)) ||
                       (!strcmp(purpose, "settings") && !role(c, AUTH_SETTINGS)) ||
                       (strcmp(purpose, "unlock") && strcmp(purpose, "settings"))))) {
        reply = lm_failure("caller cannot request this authentication purpose");
        goto done;
    }
    if ((changing || !strcmp(purpose, "settings")) && d->locked) {
        reply = lm_failure("unlock the device before changing sensitive settings");
        goto done;
    }
    if (!changing && !strcmp(purpose, "unlock") && !d->locked) {
        reply = lm_failure("session is already unlocked");
        goto done;
    }
    const char *secret = lm_string(lm_get(r, "secret"), ""),
               *replacement = lm_string(lm_get(r, "new_secret"), "");
    const char *kind = lm_string(lm_get(r, "kind"), "");
    if (strlen(secret) > 128 || strlen(action) > 64 ||
        (changing && !auth_secret_valid(kind, replacement))) {
        reply = lm_failure("PIN needs 6–12 digits; password needs 8–128 bytes without controls");
        goto done;
    }
    if (d->policy.deadline > auth_now()) {
        reply = lm_failure("authentication temporarily locked");
        goto done;
    }
    if (d->job.active) {
        reply = lm_failure("authentication busy; retry shortly");
        goto done;
    }
    pthread_mutex_lock(&d->job.mutex);
    struct auth_job *j = &d->job;
    j->active = 1;
    j->ready = 0;
    j->client = index;
    j->sequence = c->sequence;
    j->changing = changing;
    j->need_old = strcmp(d->policy.kind, "none") != 0;
    j->old_hash = d->policy.hash;
    j->generation = d->policy.generation;
    strcpy(j->secret, secret);
    if (changing) {
        strcpy(j->replacement, replacement);
        strcpy(j->kind, kind);
    }
    strcpy(j->epoch, d->epoch);
    strcpy(j->purpose, purpose);
    strcpy(j->action, action);
    c->pending = 1;
    c->deadline = auth_now() + 15000;
    pthread_cond_signal(&j->wake);
    pthread_mutex_unlock(&j->mutex);
done:
    clear_fields(r);
    lm_free(r);
    if (reply)
        auth_reply(d, index, reply);
}
void auth_complete(struct auth_daemon *d) {
    uint64_t event;
    if (read(d->job.event, &event, sizeof(event)) != sizeof(event))
        return;
    pthread_mutex_lock(&d->job.mutex);
    struct auth_job *j = &d->job;
    if (!j->ready) {
        pthread_mutex_unlock(&j->mutex);
        return;
    }
    struct auth_client *c = &d->clients[j->client];
    int connected = c->fd >= 0 && c->sequence == j->sequence;
    lm_json *reply = NULL;
    if (!d->valid || j->generation != d->policy.generation)
        reply = lm_failure("credential policy changed");
    else if (j->result < 0 || j->new_result)
        reply = lm_failure("credential derivation failed");
    else if (j->result) {
        if (d->policy.failures < 100)
            ++d->policy.failures;
        int64_t delay = d->policy.failures < 5 ? 1000 : 30000;
        if (d->policy.failures > 5)
            delay <<= (d->policy.failures - 5 > 4 ? 4 : d->policy.failures - 5);
        if (delay > 300000)
            delay = 300000;
        d->policy.penalty = delay;
        d->policy.deadline = auth_now() + delay;
        if (auth_policy_save(d)) {
            d->valid = 0;
            auth_set_locked(d);
        }
        reply = lm_failure("incorrect device credential; wait before retrying");
    } else {
        int unchanged = !strcmp(j->epoch, d->epoch);
        d->policy.failures = 0;
        d->policy.penalty = d->policy.deadline = 0;
        if (j->changing && connected && unchanged && !d->locked &&
            d->policy.generation < INT64_MAX) {
            strcpy(d->policy.kind, j->kind);
            d->policy.hash = j->new_hash;
            ++d->policy.generation;
            credential_clear(d->grants, sizeof(d->grants));
        } else if (j->changing)
            reply = lm_failure("session changed; credential update cancelled");
        if (auth_policy_save(d)) {
            d->valid = 0;
            auth_set_locked(d);
            lm_free(reply);
            reply = lm_failure("cannot persist device credentials");
        }
        if (!reply && connected) {
            reply = auth_status(d);
            if (!j->changing) {
                char token[65];
                if (!unchanged || (strcmp(j->purpose, "unlock") && d->locked) ||
                    grant_create(d, c, j, token)) {
                    lm_free(reply);
                    reply = lm_failure("session changed or grant capacity exhausted");
                } else {
                    lm_set_string(reply, "token", token);
                    credential_clear(token, sizeof(token));
                }
            }
        }
    }
    if (connected) {
        c->pending = 0;
        if (reply)
            auth_reply(d, j->client, reply);
    } else
        lm_free(reply);
    credential_clear(&j->old_hash, sizeof(j->old_hash));
    credential_clear(&j->new_hash, sizeof(j->new_hash));
    j->active = j->ready = 0;
    pthread_mutex_unlock(&j->mutex);
}
void auth_tick(struct auth_daemon *d) {
    if (d->valid && d->policy.penalty && d->policy.deadline <= auth_now()) {
        d->policy.penalty = d->policy.deadline = 0;
        if (auth_policy_save(d)) {
            d->valid = 0;
            auth_set_locked(d);
        }
    }
    if (d->watch >= 0 && auth_now() - d->heartbeat >= 250) {
        struct auth_client *c = &d->clients[d->watch];
        if (!c->size) {
            auth_reply(d, d->watch, auth_status(d));
            d->heartbeat = auth_now();
        }
    }
}
