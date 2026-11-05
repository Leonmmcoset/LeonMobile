#include "intent.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int main(int argc, char **argv) {
    struct intents state = {0};
    struct lm_daemon d = {.options = {"/run/leon/intents.sock", "/data/leon/system/default-apps.json",
        "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0}, .data = &state,
        .handle = intent_handle, .tick = intent_tick};
    if (lm_options(argc, argv, &d.options))
        return 2;
    state.defaults = lm_read_json(d.options.state);
    if (!state.defaults) {
        struct stat st;
        if (!lstat(d.options.state, &st) || errno != ENOENT)
            return 1;
        state.defaults = lm_object();
    }
    if (!lm_is_object(state.defaults) || lm_size(state.defaults) > 64) {
        lm_free(state.defaults);
        return 1;
    }
    lm_json *keys = lm_keys(state.defaults);
    int valid = 1;
    for (size_t i = 0; i < lm_size(keys); ++i) {
        const char *key = lm_string(lm_at(keys, i), "");
        const lm_json *entry = lm_get(state.defaults, key);
        const char *id = lm_string(lm_get(entry, "id"), "");
        const char *installation = lm_string(lm_get(entry, "installation"), "");
        if (!*key || strlen(key) > 159 || !lm_is_object(entry) || lm_size(entry) != 2 ||
            !*id || strlen(id) > 96 || !*installation || strlen(installation) > 32)
            valid = 0;
    }
    lm_free(keys);
    if (!valid) {
        lm_free(state.defaults);
        return 1;
    }
    int result = lm_serve(&d);
    for (size_t i = 0; i < 32; ++i)
        if (state.items[i].token[0])
            intent_remove(&d, &state.items[i]);
    lm_free(state.defaults);
    return result;
}
