#include "hardware.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int read_supply(const char *sys, const char *name, const char *field, char *text,
                       size_t size) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/class/power_supply/%s/%s", sys, name, field) >=
        (int)sizeof(path))
        return -1;
    if (lm_read_text(path, text, size))
        return -1;
    text[strcspn(text, "\n")] = 0;
    return 0;
}
void power_hardware(lm_json *out, const char *sys) {
    char path[PATH_MAX], text[128];
    lm_json *batteries = lm_array();
    int online = -1;
    snprintf(path, sizeof(path), "%s/class/power_supply", sys);
    DIR *directory = opendir(path);
    if (directory) {
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (entry->d_name[0] == '.' || strlen(entry->d_name) > 100)
                continue;
            if (read_supply(sys, entry->d_name, "type", text, sizeof(text)))
                continue;
            if (!strcmp(text, "Battery")) {
                lm_json *battery = lm_object();
                lm_set_string(battery, "name", entry->d_name);
                if (!read_supply(sys, entry->d_name, "capacity", text, sizeof(text))) {
                    long value = strtol(text, NULL, 10);
                    if (value >= 0 && value <= 100)
                        lm_set_integer(battery, "percent", value);
                }
                if (!read_supply(sys, entry->d_name, "status", text, sizeof(text)))
                    lm_set_string(battery, "status", text);
                lm_append(batteries, battery);
            } else if (!read_supply(sys, entry->d_name, "online", text, sizeof(text))) {
                if (online < 0)
                    online = 0;
                if (!strcmp(text, "1"))
                    online = 1;
            }
        }
        closedir(directory);
    }
    lm_set_boolean(out, "battery_available", lm_size(batteries) > 0);
    lm_set(out, "batteries", batteries);
    lm_set_integer(out, "external_power", online);
    snprintf(path, sizeof(path), "%s/power/state", sys);
    text[0] = 0;
    lm_read_text(path, text, sizeof(text));
    lm_set_string(out, "suspend_states", text);
}
