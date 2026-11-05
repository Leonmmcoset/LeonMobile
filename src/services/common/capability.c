#include "capability.h"
#include <errno.h>
#include <string.h>
#include <sys/random.h>
int lm_token(char out[33]) {
    unsigned char data[16];
    size_t used = 0;
    while (used < sizeof(data)) {
        ssize_t n = getrandom(data + used, sizeof(data) - used, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        used += (size_t)n;
    }
    lm_hex_encode(data, sizeof(data), out);
    return 0;
}
void lm_hex_encode(const void *data, size_t size, char *hex) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hex[2 * i] = "0123456789abcdef"[bytes[i] >> 4];
        hex[2 * i + 1] = "0123456789abcdef"[bytes[i] & 15];
    }
    hex[size * 2] = 0;
}
int lm_hex_decode(const char *hex, void *data, size_t capacity) {
    static const char digits[] = "0123456789abcdef";
    size_t size = strlen(hex);
    if (size % 2 || size / 2 > capacity)
        return -1;
    unsigned char *bytes = data;
    for (size_t i = 0; i < size; i += 2) {
        const char *a = strchr(digits, hex[i]);
        const char *b = strchr(digits, hex[i + 1]);
        if (!a || !b)
            return -1;
        bytes[i / 2] = (unsigned char)(((a - digits) << 4) | (b - digits));
    }
    return (int)(size / 2);
}
int lm_identity_matches(struct lm_daemon *d, uid_t uid, const char *id, const char *instance) {
    lm_json *app = lm_app_identity(d->options.root, d->options.builtin, uid);
    int valid = app && !strcmp(lm_string(lm_get(app, "id"), ""), id) &&
                !strcmp(lm_string(lm_get(app, "installation"), ""), instance);
    lm_free(app);
    return valid;
}
int lm_declares(const lm_json *app, const char *permission) {
    const lm_json *permissions = lm_get(app, "permissions");
    for (size_t i = 0; i < lm_size(permissions); ++i)
        if (!strcmp(lm_string(lm_at(permissions, i), ""), permission))
            return 1;
    return 0;
}
