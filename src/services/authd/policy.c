#include "authd.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
int64_t auth_now(void) {
    struct timespec now;
    clock_gettime(CLOCK_BOOTTIME, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
int auth_policy_open(struct auth_daemon *d) {
    if (lm_mkdir(d->directory, 0700))
        return -1;
    struct stat info;
    if (lstat(d->directory, &info) || info.st_uid || (info.st_mode & 077))
        return -1;
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/daemon.lock", d->directory) >= (int)sizeof(path))
        return -1;
    d->singleton = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (d->singleton < 0 || fstat(d->singleton, &info) || !S_ISREG(info.st_mode) || info.st_uid ||
        info.st_nlink != 1 || (info.st_mode & 077) || flock(d->singleton, LOCK_EX | LOCK_NB))
        return -1;
    return 0;
}
int auth_policy_save(struct auth_daemon *d) {
    char path[PATH_MAX], salt[33], verifier[65];
    if (snprintf(path, sizeof(path), "%s/policy.json", d->directory) >= (int)sizeof(path))
        return -1;
    credential_hex(d->policy.hash.salt, 16, salt);
    credential_hex(d->policy.hash.verifier, 32, verifier);
    lm_json *j = lm_object();
    lm_set_integer(j, "format", 1);
    lm_set_string(j, "kind", d->policy.kind);
    lm_set_integer(j, "generation", d->policy.generation);
    lm_set_integer(j, "failures", d->policy.failures);
    lm_set_integer(j, "deadline", d->policy.deadline);
    lm_set_integer(j, "penalty", d->policy.penalty);
    lm_set_string(j, "boot", d->policy.boot);
    lm_set_string(j, "salt", salt);
    lm_set_string(j, "verifier", verifier);
    lm_set_string(j, "kdf", "scrypt-32768-8-1");
    int result = lm_write_json(path, j);
    lm_free(j);
    return result;
}
int auth_policy_load(struct auth_daemon *d) {
    char path[PATH_MAX], text[2048];
    d->policy = (struct auth_policy){.kind = "none", .generation = 1};
    snprintf(d->policy.boot, sizeof(d->policy.boot), "%s", d->boot);
    if (snprintf(path, sizeof(path), "%s/policy.json", d->directory) >= (int)sizeof(path))
        return -1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT ? auth_policy_save(d) : -1;
    struct stat info;
    ssize_t count = read(fd, text, sizeof(text));
    int invalid = fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
                  (info.st_mode & 077) || count <= 0 || count == (ssize_t)sizeof(text);
    close(fd);
    if (invalid)
        return -1;
    lm_json *j = lm_parse(text, (size_t)count);
    credential_clear(text, sizeof(text));
    const char *kind = lm_string(lm_get(j, "kind"), ""), *boot = lm_string(lm_get(j, "boot"), "");
    d->policy.generation = lm_integer(lm_get(j, "generation"), -1);
    d->policy.failures = lm_integer(lm_get(j, "failures"), -1);
    d->policy.deadline = lm_integer(lm_get(j, "deadline"), -1);
    d->policy.penalty = lm_integer(lm_get(j, "penalty"), -1);
    invalid = lm_integer(lm_get(j, "format"), 0) != 1 ||
              (strcmp(kind, "none") && strcmp(kind, "pin") && strcmp(kind, "password")) ||
              d->policy.generation < 1 || d->policy.failures < 0 || d->policy.failures > 100 ||
              d->policy.penalty < 0 || d->policy.penalty > 300000 || d->policy.deadline < 0 ||
              !*boot || strlen(boot) > 64 ||
              strcmp(lm_string(lm_get(j, "kdf"), ""), "scrypt-32768-8-1") ||
              credential_unhex(lm_string(lm_get(j, "salt"), ""), d->policy.hash.salt, 16) ||
              credential_unhex(lm_string(lm_get(j, "verifier"), ""), d->policy.hash.verifier, 32);
    if (!invalid) {
        strcpy(d->policy.kind, kind);
        strcpy(d->policy.boot, boot);
        if (strcmp(boot, d->boot)) {
            strcpy(d->policy.boot, d->boot);
            d->policy.deadline = d->policy.penalty ? auth_now() + d->policy.penalty : 0;
        }
    }
    lm_free(j);
    return invalid ? -1 : auth_policy_save(d);
}
int auth_secret_valid(const char *kind, const char *secret) {
    size_t n = strlen(secret);
    if (!strcmp(kind, "none"))
        return !n;
    if (!strcmp(kind, "pin")) {
        if (n < 6 || n > 12)
            return 0;
        for (size_t i = 0; i < n; ++i)
            if (secret[i] < '0' || secret[i] > '9')
                return 0;
        return 1;
    }
    if (strcmp(kind, "password") || n < 8 || n > 128)
        return 0;
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)secret[i] < 32 || (unsigned char)secret[i] == 127)
            return 0;
    return 1;
}
