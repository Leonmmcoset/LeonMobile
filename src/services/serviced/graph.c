#include "manager.h"
#include "policy.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int manager_index(const struct manager *manager, const char *s) {
    for (int i = 0; i < manager->count; ++i)
        if (!strcmp(manager->services[i].name, s))
            return i;
    return -1;
}

int manager_load(struct manager *manager, const char *dir) {
    struct dirent **entries;
    int n = scandir(dir, &entries, NULL, alphasort);
    if (n < 0) {
        leon_boot_error(dir);
        return -1;
    }
    char error[512];
    int result = 0;
    for (int i = 0; i < n; ++i) {
        size_t len = strlen(entries[i]->d_name);
        if (len > 5 && !strcmp(entries[i]->d_name + len - 5, ".toml")) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir, entries[i]->d_name);
            if (manager->count == LM_MAX_SERVICES ||
                service_load(path, &manager->services[manager->count], error, sizeof(error))) {
                leon_boot_log("leond: %s\n",
                        manager->count == LM_MAX_SERVICES ? "too many services" : error);
                result = -1;
            } else {
                if (service_policy_apply(manager->policy_directory,
                                         &manager->services[manager->count]))
                    leon_boot_log("leond: invalid developer policy for %s; using built-in policy\n",
                                  manager->services[manager->count].name);
                ++manager->count;
            }
        }
        free(entries[i]);
    }
    free(entries);
    if (!result &&
        service_order(manager->services, manager->count, manager->order, error, sizeof(error))) {
        leon_boot_log("leond: %s\n", error);
        return -1;
    }
    return result;
}

int manager_dependencies_ready(const struct manager *manager, const struct service *s) {
    for (int i = 0; s->after[i]; ++i) {
        int j = manager_index(manager, s->after[i]);
        if (j < 0 || (manager->services[j].oneshot ? manager->services[j].state != FINISHED
                                                   : manager->services[j].state != RUNNING))
            return 0;
    }
    return 1;
}
