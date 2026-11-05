#include "settings.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int settings_save(struct lm_daemon *d, const lm_json *values, int64_t generation) {
    lm_json *j = lm_object();
    lm_set_integer(j, "format", 1);
    lm_set_integer(j, "generation", generation);
    lm_set(j, "settings", lm_copy(values));
    int result = lm_write_json(d->options.state, j);
    lm_free(j);
    return result;
}
int settings_load(struct lm_daemon *d) {
    struct settings_store *s = d->data;
    lm_json *j = lm_read_json(d->options.state);
    struct stat st;
    if (j) {
        const lm_json *values = lm_get(j, "settings");
        int64_t generation = lm_integer(lm_get(j, "generation"), -1);
        if (lm_size(j) != 3 || lm_integer(lm_get(j, "format"), 0) != 1 ||
            generation < 1 || !settings_validate(values, 1)) {
            lm_free(j);
            return -1;
        }
        s->values = lm_copy(values);
        s->generation = generation;
        lm_free(j);
        return 0;
    }
    if (!lstat(d->options.state, &st) || errno != ENOENT)
        return -1;
    s->values = settings_defaults();
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/settings.json", d->options.root) >= (int)sizeof(path))
        return -1;
    j = lm_read_json(path);
    if (j) {
        if (!settings_validate(j, 0)) {
            lm_free(j);
            return -1;
        }
        const char *keys[] = {"dark", "reduce_motion", "language", "navigation", "volume"};
        for (size_t i = 0; i < 5; ++i)
            if (lm_get(j, keys[i]))
                lm_set(s->values, keys[i], lm_copy(lm_get(j, keys[i])));
        lm_free(j);
    } else if (!lstat(path, &st) || errno != ENOENT)
        return -1;
    s->generation = 1;
    return settings_save(d, s->values, s->generation);
}
