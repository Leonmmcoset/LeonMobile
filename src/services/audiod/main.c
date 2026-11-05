#include "audio.h"
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
int main(int argc, char **argv) {
    struct audio state = {.volume = 70};
    struct lm_daemon d = {.options = {"/run/leon/audio.sock", "/data/leon/system/audio.json",
                                      "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &state,
                          .handle = audio_handle,
                          .tick = audio_tick};
    if (lm_options(argc, argv, &d.options))
        return 2;
    state.test = d.options.test;
    lm_json *stored = lm_read_json(d.options.state);
    if (stored) {
        state.muted = lm_boolean(lm_get(stored, "muted"), -1);
        const lm_json *apps = lm_get(stored, "apps");
        if (lm_integer(lm_get(stored, "format"), 0) != 1 || state.muted < 0 ||
            !lm_is_object(apps) || lm_size(apps) > 512) {
            lm_free(stored);
            return 1;
        }
        lm_json *keys = lm_keys(apps);
        for (size_t i = 0; i < lm_size(keys); ++i) {
            int64_t n = lm_integer(lm_get(apps, lm_string(lm_at(keys, i), "")), -1);
            if (n < 0 || n > 100) {
                lm_free(keys);
                lm_free(stored);
                return 1;
            }
        }
        lm_free(keys);
        state.volumes = lm_copy(apps);
        lm_free(stored);
    } else {
        struct stat st;
        if (!lstat(d.options.state, &st) || errno != ENOENT)
            return 1;
        state.volumes = lm_object();
    }
    int result = lm_serve(&d);
    for (size_t i = 0; i < AUDIO_STREAMS; ++i)
        if (state.streams[i].token[0])
            audio_close(&state, &state.streams[i]);
    audio_backend_free(state.backend);
    lm_free(state.volumes);
    return result;
}
