#include "intent.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
static int printable(const char *s, size_t max) {
    if (strlen(s) > max)
        return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
        if (*p < 32 || *p == 127)
            return 0;
    return 1;
}
static int mime_valid(const char *s) {
    const char *slash = strchr(s, '/');
    if (!slash || slash == s || !slash[1] || strlen(s) > 96 || strchr(slash + 1, '/'))
        return 0;
    for (const char *p = s; *p; ++p)
        if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || strchr("/.+-", *p)))
            return 0;
    return 1;
}
lm_json *intent_validate(const lm_json *r, char key[160]) {
    const char *fields[] = {"uri", "mime", "document", "action"};
    for (size_t i = 0; i < 4; ++i)
        if (lm_get(r, fields[i]) && !lm_is_string(lm_get(r, fields[i])))
            return NULL;
    const char *action = lm_string(lm_get(r, "action"), "");
    if (strcmp(action, "open") && strcmp(action, "share"))
        return NULL;
    const char *uri = lm_string(lm_get(r, "uri"), "");
    const char *mime = lm_string(lm_get(r, "mime"), "");
    const char *document = lm_string(lm_get(r, "document"), "");
    const lm_json *text = lm_get(r, "text");
    if (!!*uri + !!*document + !!text != 1)
        return NULL;
    lm_json *payload = lm_object();
    lm_set_string(payload, "action", action);
    if (*uri) {
        const char *colon = strchr(uri, ':');
        if (strcmp(action, "open") || !printable(uri, 2048) || !colon || colon == uri ||
            colon - uri > 32 || !colon[1])
            goto fail;
        char scheme[33];
        memcpy(scheme, uri, (size_t)(colon - uri));
        scheme[colon - uri] = 0;
        for (size_t i = 0; scheme[i]; ++i)
            if (!islower((unsigned char)scheme[i]) &&
                (!i || (!isdigit((unsigned char)scheme[i]) && !strchr("+.-", scheme[i]))))
                goto fail;
        if (!strcmp(scheme, "file") || !strcmp(scheme, "data") ||
            !strcmp(scheme, "javascript") || !strcmp(scheme, "shell"))
            goto fail;
        if ((!strcmp(scheme, "http") || !strcmp(scheme, "https")) &&
            (strncmp(colon + 1, "//", 2) || !colon[3] || colon[3] == '/'))
            goto fail;
        lm_set_string(payload, "uri", uri);
        lm_set_string(payload, "scheme", scheme);
        snprintf(key, 160, "%s:scheme:%s", action, scheme);
    } else {
        if (!mime_valid(mime))
            goto fail;
        lm_set_string(payload, "mime", mime);
        snprintf(key, 160, "%s:mime:%s", action, mime);
        if (text) {
            const char *s = lm_string(text, NULL);
            if (!s || strlen(s) > 2048 || strcmp(mime, "text/plain"))
                goto fail;
            lm_set_string(payload, "text", s);
        } else {
            if (strlen(document) != 32 || strspn(document, "0123456789abcdef") != 32)
                goto fail;
            lm_set_string(payload, "document", document);
        }
    }
    return payload;
fail:
    lm_free(payload);
    return NULL;
}
int intent_matches(const lm_json *app, const lm_json *p) {
    if (!strcmp(lm_string(lm_get(p, "mime"), ""), "application/vnd.leonmobile.package") &&
        (!lm_boolean(lm_get(app, "builtin"), 0) ||
         strcmp(lm_string(lm_get(app, "id"), ""), "org.leon.installer") ||
         strcmp(lm_string(lm_get(p, "action"), ""), "open") || !lm_get(p, "document")))
        return 0;
    const lm_json *handlers = lm_get(app, "handlers");
    for (size_t i = 0; i < lm_size(handlers); ++i) {
        const lm_json *h = lm_at(handlers, i);
        if (strcmp(lm_string(lm_get(h, "action"), ""), lm_string(lm_get(p, "action"), "")))
            continue;
        const char *scheme = lm_string(lm_get(h, "scheme"), "");
        if (*scheme && !strcmp(scheme, lm_string(lm_get(p, "scheme"), "")))
            return 1;
        const char *mime = lm_string(lm_get(h, "mime"), ""), *actual = lm_string(lm_get(p, "mime"), "");
        size_t n = strlen(mime);
        if (n && (!strcmp(mime, actual) || (mime[n - 1] == '*' && !strncmp(mime, actual, n - 1))))
            return 1;
    }
    return 0;
}
lm_json *intent_candidates(struct lm_daemon *d, const lm_json *payload) {
    lm_json *apps = lm_apps(d->options.root, d->options.builtin), *out = lm_array();
    for (size_t i = 0; i < lm_size(apps); ++i) {
        const lm_json *app = lm_at(apps, i);
        if (intent_matches(app, payload)) {
            lm_json *item = lm_object();
            lm_set_string(item, "id", lm_string(lm_get(app, "id"), ""));
            lm_set_string(item, "name", lm_string(lm_get(app, "name"), ""));
            lm_set_string(item, "name_en", lm_string(lm_get(app, "name_en"), ""));
            lm_append(out, item);
        }
    }
    lm_free(apps);
    return out;
}
