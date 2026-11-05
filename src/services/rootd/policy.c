#include "rootd.h"
#include <errno.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static int64_t wall_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}
static int path(const struct root_options *o, char *out, size_t size, const char *name) {
    return snprintf(out, size, "%s/%s", o->directory, name) >= (int)size ? -1 : 0;
}
static int directory(const struct root_options *o) {
    if (lm_mkdir(o->directory, 0700))
        return -1;
    struct stat info;
    return lstat(o->directory, &info) || info.st_uid || (info.st_mode & 077) ? -1 : 0;
}
int root_policy_lock(const struct root_options *o) {
    char name[PATH_MAX];
    if (directory(o) || path(o, name, sizeof(name), "policy.lock"))
        return -1;
    int fd = open(name, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat info;
    if (fd < 0)
        return -1;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
        (info.st_mode & 077) || flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd);
        return -1;
    }
    return fd;
}
static int hex_read(const char *text, unsigned char *bytes, size_t size) {
    if (strlen(text) != size * 2)
        return -1;
    for (size_t i = 0; i < size; ++i) {
        unsigned value = 0;
        for (size_t j = 0; j < 2; ++j) {
            unsigned char c = text[i * 2 + j];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return -1;
            value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        bytes[i] = (unsigned char)value;
    }
    return 0;
}
static void hex_write(const unsigned char *bytes, size_t size, char *out) {
    for (size_t i = 0; i < size; ++i)
        snprintf(out + i * 2, 3, "%02x", bytes[i]);
}
int root_policy_load(const struct root_options *o, struct root_policy *p) {
    *p = (struct root_policy){0};
    char name[PATH_MAX], text[4096];
    if (directory(o) || path(o, name, sizeof(name), "policy.json"))
        return -1;
    int fd = open(name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    struct stat info;
    ssize_t count;
    do {
        count = read(fd, text, sizeof(text));
    } while (count < 0 && errno == EINTR);
    /* Only root can publish in this 0700 directory. Atomic replacement may
     * unlink a successfully opened inode, but its pinned descriptor still holds
     * a complete policy snapshot. Reject extra hardlinks, not that legitimate
     * zero-link snapshot; the next read observes the newly published policy. */
    int invalid = fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink > 1 ||
                  (info.st_mode & 077) || count <= 0 || count == sizeof(text);
    close(fd);
    if (invalid)
        return -1;
    lm_json *j = lm_parse(text, (size_t)count);
    root_clear(text, sizeof(text));
    const char *salt = lm_string(lm_get(j, "salt"), ""), *hash = lm_string(lm_get(j, "hash"), "");
    unsigned char bytes[32];
    p->enabled = lm_boolean(lm_get(j, "enabled"), -1);
    p->generation = lm_integer(lm_get(j, "generation"), -1);
    int64_t failures = lm_integer(lm_get(j, "failures"), -1);
    p->failures = failures >= 0 && failures <= 5 ? (int)failures : -1;
    p->retry_until = lm_integer(lm_get(j, "retry_until"), -1);
    invalid = lm_integer(lm_get(j, "format"), 0) != 1 || p->enabled < 0 || p->generation < 1 ||
              p->failures < 0 || p->retry_until < 0 ||
              strcmp(lm_string(lm_get(j, "kdf"), ""), "scrypt-32768-8-1") ||
              hex_read(salt, bytes, 16) || hex_read(hash, bytes, 32);
    if (!invalid) {
        strcpy(p->salt, salt);
        strcpy(p->hash, hash);
        p->present = 1;
    }
    lm_free(j);
    return invalid ? -1 : 0;
}
int root_policy_save(const struct root_options *o, const struct root_policy *p) {
    char name[PATH_MAX];
    if (path(o, name, sizeof(name), "policy.json"))
        return -1;
    lm_json *j = lm_object();
    lm_set_integer(j, "format", 1);
    lm_set_boolean(j, "enabled", p->enabled);
    lm_set_integer(j, "generation", p->generation);
    lm_set_integer(j, "failures", p->failures);
    lm_set_integer(j, "retry_until", p->retry_until);
    lm_set_string(j, "kdf", "scrypt-32768-8-1");
    lm_set_string(j, "salt", p->salt);
    lm_set_string(j, "hash", p->hash);
    int result = lm_write_json(name, j);
    lm_free(j);
    return result;
}
static int derive(const char *password, const unsigned char *salt, unsigned char *hash) {
    return EVP_PBE_scrypt(password, strlen(password), salt, 16, 32768, 8, 1, 64 * 1024 * 1024, hash,
                          32) == 1
               ? 0
               : -1;
}
int root_set_password(struct root_policy *p, const char *password) {
    size_t size = strlen(password);
    if (size < 8 || size > 128)
        return -1;
    for (size_t i = 0; i < size; ++i)
        if ((unsigned char)password[i] < 32 || (unsigned char)password[i] == 127)
            return -1;
    unsigned char salt[16], hash[32];
    if (RAND_bytes(salt, sizeof(salt)) != 1 || derive(password, salt, hash))
        return -1;
    hex_write(salt, sizeof(salt), p->salt);
    hex_write(hash, sizeof(hash), p->hash);
    root_clear(hash, sizeof(hash));
    p->present = 1;
    p->failures = 0;
    p->retry_until = 0;
    return 0;
}
int root_authenticate(const struct root_options *o, struct root_policy *p, const char *password) {
    if (!p->present || p->retry_until > wall_ms() || strlen(password) > 128)
        return -1;
    unsigned char salt[16], expected[32], hash[32];
    if (hex_read(p->salt, salt, sizeof(salt)) || hex_read(p->hash, expected, sizeof(expected)) ||
        derive(password, salt, hash))
        return -1;
    int valid = CRYPTO_memcmp(hash, expected, sizeof(hash)) == 0;
    root_clear(hash, sizeof(hash));
    root_clear(expected, sizeof(expected));
    p->failures = valid ? 0 : p->failures < 5 ? p->failures + 1 : 5;
    p->retry_until = valid ? 0 : wall_ms() + (p->failures >= 5 ? 30000 : 1000);
    return root_policy_save(o, p) || !valid ? -1 : 0;
}
lm_json *root_policy_status(const struct root_policy *p) {
    lm_json *out = lm_success();
    lm_set_boolean(out, "enabled", p->enabled);
    lm_set_boolean(out, "password_set", p->present);
    lm_set_boolean(out, "sudo_enabled", 0);
    int64_t retry = p->retry_until - wall_ms();
    lm_set_integer(out, "retry_after_ms", retry > 0 ? retry : 0);
    return out;
}
