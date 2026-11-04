#include "service.h"
#include "daemon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct memory_state {
    long long available, total, next, reclaimed;
    unsigned threshold;
    char action[128];
};
static void sample(struct lm_daemon *d) {
    struct memory_state *s = d->data;
    char path[PATH_MAX], text[16384];
    snprintf(path, sizeof(path), "%s/meminfo", d->options.proc);
    s->available = s->total = -1;
    if (lm_read_text(path, text, sizeof(text)))
        return;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        long long n;
        if (sscanf(line, "MemAvailable: %lld kB", &n) == 1)
            s->available = n;
        if (sscanf(line, "MemTotal: %lld kB", &n) == 1)
            s->total = n;
    }
}
static void tick(struct lm_daemon *d) {
    struct memory_state *s = d->data;
    if (lm_now_ms() < s->next)
        return;
    s->next = lm_now_ms() + 2000;
    sample(d);
    if (s->available < 0 || s->available >= (long long)s->threshold * 1024 || d->options.test)
        return;
    lm_json *request = lm_object();
    lm_set_string(request, "op", "reclaim");
    lm_json *response = lm_rpc("/run/leon/apps.sock", request);
    if (lm_boolean(lm_get(response, "ok"), 0)) {
        const char *app = lm_string(lm_get(response, "reclaimed"), "");
        snprintf(s->action, sizeof(s->action), "%s", app);
        if (*app)
            ++s->reclaimed;
        s->next = lm_now_ms() + 5000;
    }
    lm_free(response);
    lm_free(request);
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct memory_state *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "policy")) {
        if (uid)
            return lm_failure("permission denied");
        long long value = lm_integer(lm_get(r, "min_available_mb"), -1);
        if (value < 16 || value > 1024)
            return lm_failure("min_available_mb must be 16..1024");
        lm_json *policy = lm_object();
        lm_set_integer(policy, "min_available_mb", value);
        if (lm_write_json(d->options.state, policy)) {
            lm_free(policy);
            return lm_failure("cannot persist policy");
        }
        lm_free(policy);
        s->threshold = (unsigned)value;
    } else if (strcmp(op, "status"))
        return lm_failure("unknown operation");
    sample(d);
    lm_json *out = lm_success();
    lm_set_integer(out, "available_kb", s->available);
    lm_set_integer(out, "total_kb", s->total);
    lm_set_integer(out, "min_available_mb", s->threshold);
    lm_set_integer(out, "reclaimed", s->reclaimed);
    lm_set_string(out, "last_reclaimed", s->action);
    char path[PATH_MAX], pressure[1024] = "";
    snprintf(path, sizeof(path), "%s/pressure/memory", d->options.proc);
    lm_set_boolean(out, "psi_available", !lm_read_text(path, pressure, sizeof(pressure)));
    lm_set_string(out, "pressure", pressure);
    return out;
}
int memd_run(int argc, char **argv) {
    struct memory_state state = {.threshold = 96};
    struct lm_daemon d = {.options = {"/run/leon/memory.sock", "/data/leon/system/memory.json",
                                      "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &state,
                          .handle = handle,
                          .tick = tick};
    if (lm_options(argc, argv, &d.options))
        return 2;
    lm_json *policy = lm_read_json(d.options.state);
    long long value = lm_integer(lm_get(policy, "min_available_mb"), 96);
    if (value >= 16 && value <= 1024)
        state.threshold = (unsigned)value;
    lm_free(policy);
    return lm_serve(&d);
}
