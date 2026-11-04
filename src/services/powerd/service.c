#include "service.h"
#include "auth.h"
#include "daemon.h"
#include "hardware.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct inhibitor {
    uid_t uid;
    unsigned token;
    long long expires;
    char reason[96];
};
struct power_state {
    long long activity, sampled;
    unsigned idle_seconds, sequence;
    int locked, requested;
    struct inhibitor inhibitors[32];
};
static void expire(struct power_state *s) {
    for (size_t i = 0; i < 32; ++i)
        if (s->inhibitors[i].token && lm_now_ms() >= s->inhibitors[i].expires)
            s->inhibitors[i].token = 0;
}
static int inhibited(struct power_state *s) {
    expire(s);
    for (size_t i = 0; i < 32; ++i)
        if (s->inhibitors[i].token)
            return 1;
    return 0;
}
static void tick(struct lm_daemon *d) {
    struct power_state *s = d->data;
    if (!d->options.test && lm_now_ms() - s->sampled >= 200) {
        s->sampled = lm_now_ms();
        struct auth_state state;
        char response[LEON_AUTH_LIMIT];
        int locked = auth_exchange(LEON_AUTH_SOCKET, "{\"op\":\"status\"}\n", response, 100) ||
                     auth_decode(response, &state) || !state.ok || state.locked;
        if (s->locked && !locked)
            s->activity = lm_now_ms();
        s->locked = locked;
        s->requested = locked;
    }
    if (!s->locked && s->idle_seconds && !inhibited(s) &&
        lm_now_ms() - s->activity >= (long long)s->idle_seconds * 1000) {
        s->requested = 1;
        if (!d->options.test) {
            (void)auth_lock(LEON_AUTH_SOCKET);
            s->locked = 1;
        }
    }
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct power_state *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    lm_json *out = lm_success();
    if (!strcmp(op, "inhibit")) {
        lm_json *app = lm_app(d->options.root, d->options.builtin, uid);
        if (!lm_trusted(uid) && !app) {
            lm_free(out);
            return lm_failure("unknown application identity");
        }
        lm_free(app);
        long long seconds = lm_integer(lm_get(r, "seconds"), -1);
        const char *reason = lm_string(lm_get(r, "reason"), "");
        if (seconds < 1 || seconds > 300 || !*reason || strlen(reason) > 95) {
            lm_free(out);
            return lm_failure("invalid inhibitor duration or reason");
        }
        expire(s);
        unsigned owned = 0;
        for (size_t i = 0; i < 32; ++i)
            if (s->inhibitors[i].token && s->inhibitors[i].uid == uid)
                ++owned;
        if (!lm_trusted(uid) && owned >= 4) {
            lm_free(out);
            return lm_failure("application inhibitor limit reached");
        }
        for (size_t i = 0; i < 32; ++i)
            if (!s->inhibitors[i].token) {
                s->inhibitors[i] = (struct inhibitor){
                    .uid = uid, .token = ++s->sequence, .expires = lm_now_ms() + seconds * 1000};
                snprintf(s->inhibitors[i].reason, 96, "%s", reason);
                lm_set_integer(out, "token", s->sequence);
                return out;
            }
        lm_free(out);
        return lm_failure("inhibitor limit reached");
    } else if (!strcmp(op, "release")) {
        long long token = lm_integer(lm_get(r, "token"), -1);
        if (token < 1 || token > UINT_MAX) {
            lm_free(out);
            return lm_failure("invalid inhibitor token");
        }
        int found = 0;
        for (size_t i = 0; i < 32; ++i)
            if (token > 0 && s->inhibitors[i].token == (unsigned)token &&
                (uid == 0 || uid == s->inhibitors[i].uid)) {
                s->inhibitors[i].token = 0;
                found = 1;
            }
        if (!found) {
            lm_free(out);
            return lm_failure("unknown or unauthorized inhibitor");
        }
    } else {
        if (strcmp(op, "status") && !lm_trusted(uid)) {
            lm_free(out);
            return lm_failure("permission denied");
        }
        if (!strcmp(op, "activity")) {
            s->activity = lm_now_ms();
            if (d->options.test)
                s->requested = 0;
        } else if (!strcmp(op, "lock")) {
            s->requested = 1;
            if (!d->options.test) {
                if (auth_lock(LEON_AUTH_SOCKET)) {
                    lm_free(out);
                    return lm_failure(
                        "authentication service unavailable; session remains protected");
                }
                s->locked = 1;
            }
        } else if (!strcmp(op, "locked")) {
            int locked = lm_boolean(lm_get(r, "value"), -1);
            if (locked < 0) {
                lm_free(out);
                return lm_failure("value must be boolean");
            }
            if (!d->options.test) {
                lm_free(out);
                return lm_failure("lock state belongs to authd; use authenticated unlock");
            }
            s->locked = locked;
            s->requested = 0;
            s->activity = lm_now_ms();
        } else if (!strcmp(op, "policy")) {
            if (uid) {
                lm_free(out);
                return lm_failure("permission denied");
            }
            long long value = lm_integer(lm_get(r, "idle_seconds"), -1);
            if (value < 0 || value > 86400) {
                lm_free(out);
                return lm_failure("invalid idle timeout");
            }
            lm_json *policy = lm_object();
            lm_set_integer(policy, "idle_seconds", value);
            if (lm_write_json(d->options.state, policy)) {
                lm_free(policy);
                lm_free(out);
                return lm_failure("cannot persist policy");
            }
            lm_free(policy);
            s->idle_seconds = (unsigned)value;
        } else if (!strcmp(op, "poweroff") || !strcmp(op, "reboot")) {
            if (d->options.test) {
                lm_free(out);
                return lm_failure("power operations disabled for test roots");
            }
            if (kill(1, !strcmp(op, "reboot") ? SIGUSR2 : SIGUSR1)) {
                lm_free(out);
                return lm_failure("init rejected power operation");
            }
        } else if (!strcmp(op, "suspend")) {
            if (d->options.test || inhibited(s) || !s->locked) {
                lm_free(out);
                return lm_failure("suspend requires a locked session without inhibitors");
            }
            char path[PATH_MAX], states[256];
            snprintf(path, sizeof(path), "%s/power/state", d->options.sys);
            if (lm_read_text(path, states, sizeof(states)) ||
                (!strstr(states, "mem") && !strstr(states, "freeze"))) {
                lm_free(out);
                return lm_failure("suspend unavailable on this platform");
            }
            int fd = open(path, O_WRONLY | O_CLOEXEC);
            const char *mode = strstr(states, "mem") ? "mem" : "freeze";
            int result = fd < 0 ? -1 : (write(fd, mode, strlen(mode)) != (ssize_t)strlen(mode));
            if (fd >= 0)
                close(fd);
            s->activity = lm_now_ms();
            if (result) {
                lm_free(out);
                return lm_failure("platform suspend failed");
            }
        } else if (strcmp(op, "status")) {
            lm_free(out);
            return lm_failure("unknown operation");
        }
    }
    tick(d);
    lm_set_boolean(out, "locked", s->locked);
    lm_set_boolean(out, "lock_requested", s->requested);
    lm_set_boolean(out, "inhibited", inhibited(s));
    lm_set_integer(out, "idle_seconds", s->idle_seconds);
    lm_set_integer(out, "idle_ms", lm_now_ms() - s->activity);
    lm_json *list = lm_array();
    for (size_t i = 0; i < 32; ++i)
        if (s->inhibitors[i].token && (lm_trusted(uid) || s->inhibitors[i].uid == uid)) {
            lm_json *item = lm_object();
            lm_set_integer(item, "token", s->inhibitors[i].token);
            lm_set_integer(item, "uid", s->inhibitors[i].uid);
            lm_set_string(item, "reason", s->inhibitors[i].reason);
            lm_append(list, item);
        }
    lm_set(out, "inhibitors", list);
    power_hardware(out, d->options.sys);
    return out;
}
int powerd_run(int argc, char **argv) {
    struct power_state state = {.idle_seconds = 300, .locked = 1};
    state.activity = lm_now_ms();
    struct lm_daemon d = {.options = {"/run/leon/power.sock", "/data/leon/system/power.json",
                                      "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &state,
                          .handle = handle,
                          .tick = tick};
    if (lm_options(argc, argv, &d.options))
        return 2;
    lm_json *policy = lm_read_json(d.options.state);
    long long timeout = lm_integer(lm_get(policy, "idle_seconds"), 300);
    if (timeout >= 0 && timeout <= 86400)
        state.idle_seconds = (unsigned)timeout;
    lm_free(policy);
    return lm_serve(&d);
}
