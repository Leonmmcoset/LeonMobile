#include "settings.h"
#include <stdlib.h>
#include <string.h>
lm_json *settings_handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct settings_store *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "schema")) {
        lm_json *out = lm_success();
        lm_set(out, "schema", settings_schema());
        return out;
    }
    if (!strcmp(op, "set") || !strcmp(op, "update")) {
        if (!lm_trusted(uid) && uid != 11000)
            return lm_failure("permission denied");
        int64_t expected = lm_integer(lm_get(r, "expected_generation"), -1);
        if (lm_get(r, "expected_generation") && (expected < 1 || expected != s->generation))
            return lm_failure("settings generation conflict");
        lm_json *changes;
        if (!strcmp(op, "set")) {
            const char *key = lm_string(lm_get(r, "key"), "");
            if (!*key || !lm_get(r, "value"))
                return lm_failure("key and value required");
            changes = lm_object();
            lm_set(changes, key, lm_copy(lm_get(r, "value")));
        } else
            changes = lm_copy(lm_get(r, "values"));
        if (!settings_validate(changes, 0) || !lm_size(changes)) {
            lm_free(changes);
            return lm_failure("unknown setting or invalid value");
        }
        lm_json *next = lm_copy(s->values);
        const char *keys[] = {"dark", "reduce_motion", "language", "navigation", "volume"};
        int changed = 0;
        for (size_t i = 0; i < 5; ++i)
            if (lm_get(changes, keys[i])) {
                char *a = lm_dump(lm_get(changes, keys[i])), *b = lm_dump(lm_get(next, keys[i]));
                changed |= !a || !b || strcmp(a, b);
                free(a);
                free(b);
                lm_set(next, keys[i], lm_copy(lm_get(changes, keys[i])));
            }
        lm_free(changes);
        if (changed) {
            if (s->generation == INT64_MAX || settings_save(d, next, s->generation + 1)) {
                lm_free(next);
                return lm_failure("cannot persist settings");
            }
            lm_free(s->values);
            s->values = next;
            ++s->generation;
        } else
            lm_free(next);
    } else if (strcmp(op, "status") && strcmp(op, "watch"))
        return lm_failure("unknown operation");
    int64_t since = lm_integer(lm_get(r, "since"), -1);
    if (!strcmp(op, "watch") && since < 0)
        return lm_failure("since must be a nonnegative generation");
    lm_json *out = lm_success();
    lm_set_integer(out, "generation", s->generation);
    lm_set_boolean(out, "changed", since != s->generation);
    lm_set(out, "settings", lm_copy(s->values));
    return out;
}
