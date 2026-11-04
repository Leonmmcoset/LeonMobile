#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        ++s;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        --e;
    *e = 0;
    return s;
}

/* Deliberately small, strict TOML service schema: quoted strings, string arrays,
 * booleans and decimal IDs. Unknown keys and duplicate keys are errors. */
static char *string(char **cursor) {
    char *p = *cursor;
    while (isspace((unsigned char)*p))
        ++p;
    if (*p++ != '"')
        return NULL;
    char *out = calloc(strlen(p) + 1, 1), *q = out;
    if (!out)
        return NULL;
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            if (*p != '\\' && *p != '"') {
                free(out);
                return NULL;
            }
        }
        if ((unsigned char)*p < 32) {
            free(out);
            return NULL;
        }
        *q++ = *p++;
    }
    if (*p != '"') {
        free(out);
        return NULL;
    }
    *cursor = p + 1;
    return out;
}

static int array(char *p, char **out, int max) {
    if (*p++ != '[')
        return -1;
    int n = 0;
    for (;;) {
        while (isspace((unsigned char)*p))
            ++p;
        if (*p == ']')
            return *trim(p + 1) ? -1 : 0;
        if (n >= max || !(out[n] = string(&p)))
            return -1;
        ++n;
        while (isspace((unsigned char)*p))
            ++p;
        if (*p == ']')
            return *trim(p + 1) ? -1 : 0;
        if (*p++ != ',')
            return -1;
    }
}

void service_free(struct service *s) {
    for (int i = 0; i < LM_MAX_ARGS; ++i) {
        free(s->argv[i]);
        s->argv[i] = NULL;
    }
    for (int i = 0; i < LM_MAX_DEPS; ++i) {
        free(s->after[i]);
        s->after[i] = NULL;
    }
}

int service_load(const char *path, struct service *s, char *err, size_t size) {
    memset(s, 0, sizeof(*s));
    s->restart_limit = 5;
    s->autostart = 1;
    s->notify_fd = -1;
    s->ready_timeout_ms = 10000;
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, size, "%s: %s", path, strerror(errno));
        return -1;
    }
    char *line = NULL;
    size_t cap = 0;
    int number = 0, rc = -1;
    unsigned seen = 0;
    const char *keys[] = {"name", "command",          "after",         "uid",
                          "gid",  "restart",          "restart_limit", "autostart",
                          "type", "ready_timeout_ms", "watchdog_ms"};
    while (getline(&line, &cap, f) >= 0) {
        ++number;
        char *p = trim(line);
        if (!*p || *p == '#')
            continue;
        char *eq = strchr(p, '=');
        if (!eq)
            goto bad;
        *eq++ = 0;
        char *key = trim(p), *value = trim(eq);
        int k = 0;
        while (k < 11 && strcmp(key, keys[k]))
            ++k;
        if (k == 11 || (seen & (1u << k)))
            goto bad;
        seen |= 1u << k;
        if (k == 0 || k == 5 || k == 8) {
            char *v = string(&value);
            if (!v || *trim(value)) {
                free(v);
                goto bad;
            }
            if (k == 0) {
                if (!*v || strlen(v) >= sizeof(s->name)) {
                    free(v);
                    goto bad;
                }
                for (char *c = v; *c; ++c)
                    if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_') {
                        free(v);
                        goto bad;
                    }
                strcpy(s->name, v);
            } else if (k == 8) {
                if (!strcmp(v, "oneshot"))
                    s->oneshot = 1;
                else if (!strcmp(v, "notify"))
                    s->notify = 1;
                else if (strcmp(v, "simple")) {
                    free(v);
                    goto bad;
                }
            } else {
                if (!strcmp(v, "always"))
                    s->restart = 2;
                else if (!strcmp(v, "on-failure"))
                    s->restart = 1;
                else if (strcmp(v, "never")) {
                    free(v);
                    goto bad;
                }
            }
            free(v);
        } else if (k == 1 || k == 2) {
            if (array(value, k == 1 ? s->argv : s->after, k == 1 ? LM_MAX_ARGS : LM_MAX_DEPS))
                goto bad;
        } else if (k == 7) {
            if (!strcmp(value, "true"))
                s->autostart = 1;
            else if (!strcmp(value, "false"))
                s->autostart = 0;
            else
                goto bad;
        } else {
            if (!isdigit((unsigned char)*value))
                goto bad;
            errno = 0;
            char *end;
            unsigned long v = strtoul(value, &end, 10);
            if (errno || *end || v > 60000)
                goto bad;
            if (k == 3)
                s->uid = (uid_t)v;
            if (k == 4)
                s->gid = (gid_t)v;
            if (k == 6)
                s->restart_limit = (unsigned)v;
            if (k == 9)
                s->ready_timeout_ms = (unsigned)v;
            if (k == 10)
                s->watchdog_ms = (unsigned)v;
        }
    }
    if (ferror(f) || !*s->name || !s->argv[0] || s->argv[0][0] != '/')
        goto bad;
    if (!s->ready_timeout_ms || (s->watchdog_ms && s->watchdog_ms < 250) ||
        (s->oneshot && s->watchdog_ms))
        goto bad;
    rc = 0;
    goto done;
bad:
    snprintf(err, size, "%s:%d: invalid or unsupported service configuration", path, number);
    service_free(s);
done:
    free(line);
    fclose(f);
    return rc;
}

static int visit(struct service *s, size_t n, size_t i, int *marks, int *order, int *pos, char *err,
                 size_t size) {
    if (marks[i] == 2)
        return 0;
    if (marks[i] == 1) {
        snprintf(err, size, "dependency cycle at %s", s[i].name);
        return -1;
    }
    marks[i] = 1;
    for (int a = 0; s[i].after[a]; ++a) {
        size_t j = 0;
        while (j < n && strcmp(s[j].name, s[i].after[a]))
            ++j;
        if (j == n) {
            snprintf(err, size, "%s: missing dependency %s", s[i].name, s[i].after[a]);
            return -1;
        }
        if (visit(s, n, j, marks, order, pos, err, size))
            return -1;
    }
    marks[i] = 2;
    order[(*pos)++] = (int)i;
    return 0;
}

int service_order(struct service *s, size_t n, int *order, char *err, size_t size) {
    if (n > LM_MAX_SERVICES)
        return -1;
    int marks[LM_MAX_SERVICES] = {0}, pos = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(s[i].name, s[j].name)) {
                snprintf(err, size, "duplicate service %s", s[i].name);
                return -1;
            }
        if (visit(s, n, i, marks, order, &pos, err, size))
            return -1;
    }
    return 0;
}
