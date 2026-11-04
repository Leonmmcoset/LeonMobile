#include "policy.h"
#include "daemon.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int service_policy_apply(const char *directory, struct service *service) {
    char path[PATH_MAX];
    if (!directory)
        return 0;
    if (snprintf(path, sizeof(path), "%s/%s.json", directory, service->name) >= (int)sizeof(path))
        return -1;
    struct stat info;
    if (lstat(path, &info))
        return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(info.st_mode) || info.st_uid || (info.st_mode & 077) || info.st_size > 1024)
        return -1;
    lm_json *json = lm_read_json(path);
    const char *restart = lm_string(lm_get(json, "restart"), "");
    int autostart = lm_boolean(lm_get(json, "autostart"), -1);
    long long limit = lm_integer(lm_get(json, "restart_limit"), -1);
    int mode = !strcmp(restart, "never")        ? 0
               : !strcmp(restart, "on-failure") ? 1
               : !strcmp(restart, "always")     ? 2
                                                : -1;
    int invalid = !lm_is_object(json) || lm_size(json) != 3 || autostart < 0 || mode < 0 ||
                  limit < 0 || limit > 60000;
    if (!invalid) {
        service->autostart = autostart;
        service->restart = mode;
        service->restart_limit = (unsigned)limit;
    }
    lm_free(json);
    return invalid ? -1 : 0;
}
