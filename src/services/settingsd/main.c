#include "settings.h"
#include <stdio.h>
int main(int argc, char **argv) {
    struct settings_store state = {0};
    struct lm_daemon d = {.options = {"/run/leon/settings.sock", "/data/leon/system/settings.json",
        "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0},
        .data = &state, .handle = settings_handle};
    if (lm_options(argc, argv, &d.options))
        return 2;
    if (settings_load(&d)) {
        fputs("settingsd: cannot load or initialize validated settings\n", stderr);
        lm_free(state.values);
        return 1;
    }
    int result = lm_serve(&d);
    lm_free(state.values);
    return result;
}
