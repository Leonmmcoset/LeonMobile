#include "time_state.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int time_zone_valid(const char *s) {
    if (!*s || strlen(s) > 120 || s[0] == '/' || strstr(s, "..")) return 0;
    for (const char *p = s; *p; ++p)
        if (!(isalnum((unsigned char)*p) || strchr("/_+-", *p))) return 0;
    char path[PATH_MAX], magic[4];
    snprintf(path, sizeof(path), "/usr/share/zoneinfo/%s", s);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int valid = fd >= 0 && read(fd, magic, 4) == 4 && !memcmp(magic, "TZif", 4);
    if (fd >= 0) close(fd);
    return valid;
}
int time_zone_apply(struct lm_daemon *d, const char *zone) {
    if (!time_zone_valid(zone)) return -1;
    if (!d->options.test) {
        char source[PATH_MAX];
        snprintf(source, sizeof(source), "/usr/share/zoneinfo/%s", zone);
        unlink("/run/localtime.new");
        if (symlink(source, "/run/localtime.new") || rename("/run/localtime.new", "/run/localtime")) return -1;
    }
    // Test roots may change only this daemon's environment, never host clocks/files.
    setenv("TZ", zone, 1);tzset();
    return 0;
}
static int compare(const void *a, const void *b) { return strcmp(a, b); }
lm_json *time_zones(const lm_json *r) {
    const char *query = lm_string(lm_get(r, "query"), "");
    if ((lm_get(r, "query") && !lm_is_string(lm_get(r, "query"))) || strlen(query) > 120)
        return lm_failure("invalid timezone search");
    FILE *file = fopen("/usr/share/zoneinfo/zone1970.tab", "r");
    if (!file) return lm_failure("timezone catalog unavailable");
    char zones[512][128] = {"Etc/UTC"}, line[512];size_t count = 1;
    while (fgets(line, sizeof(line), file) && count < 512) {
        if (*line == '#') continue;
        char *save, *part = strtok_r(line, "\t", &save);
        if (!part || !strtok_r(NULL, "\t", &save)) continue;
        part = strtok_r(NULL, "\t\r\n", &save);
        if (part && time_zone_valid(part)) snprintf(zones[count++], 128, "%s", part);
    }
    fclose(file);qsort(zones, count, sizeof(zones[0]), compare);
    lm_json *out = lm_success(), *list = lm_array();
    for (size_t i = 0; i < count; ++i)
        if (!*query || strcasestr(zones[i], query)) {
            lm_json *item = lm_object();lm_set_string(item, "id", zones[i]);lm_append(list, item);
        }
    lm_set(out, "zones", list);return out;
}
