#include "service.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct network_state *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "status") || !strcmp(op, "watch")) {
        int64_t since = lm_integer(lm_get(r, "since"), -1);
        if (!strcmp(op, "watch") && since < 0)
            return lm_failure("since must be a nonnegative generation");
        lm_json *out = network_status(d);
        lm_set_boolean(out, "changed", since != s->generation);
        return out;
    }
    if (!lm_trusted(uid) && uid != 11000)
        return lm_failure("permission denied");
    if (!strcmp(op, "renew")) {
        if (d->options.test)
            return lm_failure("network mutations disabled for test roots");
        if (!s->wanted || strcmp(lm_string(lm_get(s->policy, "method"), ""), "dhcp"))
            return lm_failure("DHCP is not enabled");
        if (s->dhcp && kill(s->dhcp, SIGUSR1))
            return lm_failure("DHCP client unavailable");
        if (!s->dhcp) {
            s->next = 0;
            s->failures = 0;
        }
        return network_status(d);
    }
    lm_json *next = lm_copy(s->policy);
    if (!strcmp(op, "enable") || !strcmp(op, "disable")) {
        if (d->options.test) {
            lm_free(next);
            return lm_failure("network mutations disabled for test roots");
        }
        lm_set_boolean(next, "enabled", !strcmp(op, "enable"));
    } else if (!strcmp(op, "configure")) {
        const lm_json *changes = lm_get(r, "values");
        if (!lm_is_object(changes) || !lm_size(changes)) {
            lm_free(next);
            return lm_failure("nonempty values object required");
        }
        lm_json *keys = lm_keys(changes);
        int valid = 1;
        for (size_t i = 0; i < lm_size(keys); ++i) {
            const char *key = lm_string(lm_at(keys, i), "");
            if (!lm_get(next, key)) {
                valid = 0;
                break;
            }
            lm_set(next, key, lm_copy(lm_get(changes, key)));
        }
        lm_free(keys);
        if (!valid || !network_validate(next)) {
            lm_free(next);
            return lm_failure("invalid network configuration");
        }
    } else {
        lm_free(next);
        return lm_failure("unknown operation");
    }
    if (lm_write_json(s->policy_path, next)) {
        lm_free(next);
        return lm_failure("cannot persist network configuration");
    }
    network_stop(&s->apply);
    network_stop(&s->dhcp);
    lm_free(s->policy);
    s->policy = next;
    s->wanted = lm_boolean(lm_get(next, "enabled"), 1);
    s->apply_needed = !d->options.test;
    s->next = 0;
    s->failures = 0;
    s->sample = 0;
    s->error[0] = 0;
    return network_status(d);
}
int netd_run(int argc, char **argv) {
    struct network_state state = {.interface = "eth0"};
    struct lm_daemon d = {.options = {"/run/leon/network.sock", "/run/resolv.conf", "/data/leon",
                                      "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &state,
                          .handle = handle,
                          .tick = network_tick};
    if (lm_options(argc, argv, &d.options))
        return 2;
    if (network_load(&d)) {
        lm_free(state.policy);
        fputs("netd: invalid or unavailable configuration\n", stderr);
        return 1;
    }
    int result = lm_serve(&d);
    network_stop(&state.apply);
    network_stop(&state.dhcp);
    lm_free(state.policy);
    return result;
}
