#include "state.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
const char *leon_boot_mode_name(unsigned m) {
    return m <= LEON_BOOT_RECOVERY ? (const char *[]){"normal", "safe", "recovery"}[m] : "invalid";
}
const char *leon_boot_reason_name(unsigned r) {
    static const char *names[] = {"none", "previous boot unfinished", "system mount failed",
        "essential system files missing", "startup health deadline exceeded", "boot state invalid",
        "data partition unavailable", "recovery requested", "system repair interrupted",
        "system image restored", "system verification failed"};
    return r < sizeof(names) / sizeof(names[0]) ? names[r] : "invalid";
}
static int valid(const struct leon_boot_state *s) {
    if (s->attempts > 1000000 || s->failures > 1000000 || s->pending > 1 || s->requested > 1 ||
        s->mode > LEON_BOOT_RECOVERY || s->next > LEON_BOOT_RECOVERY || s->reason > LEON_BOOT_VERIFY ||
        strlen(s->boot_id) != 36) return 0;
    for (int i = 0; i < 36; ++i)
        if ((i == 8 || i == 13 || i == 18 || i == 23) ? s->boot_id[i] != '-' :
            !isxdigit((unsigned char)s->boot_id[i])) return 0;
    return 1;
}
static uint32_t checksum(const char *p, size_t n) {
    uint32_t crc = ~0U;
    while (n--) {
        crc ^= (unsigned char)*p++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return ~crc;
}
int leon_boot_store_open(struct leon_boot_store *s, const char *path) {
    *s = (struct leon_boot_store){-1, -1};
    if (!path || path[0] != '/' || strlen(path) >= PATH_MAX) { errno = EINVAL; return -1; }
    char copy[PATH_MAX]; strcpy(copy, path);
    int dir = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    char *save, *part = strtok_r(copy, "/", &save);
    while (dir >= 0 && part) {
        char *next = strtok_r(NULL, "/", &save);
        if (!strcmp(part, ".") || !strcmp(part, "..")) { close(dir); return -1; }
        if (mkdirat(dir, part, next ? 0755 : 0700) && errno != EEXIST) { close(dir); return -1; }
        int child = openat(dir, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(dir); dir = child;
        struct stat st;
        if (dir < 0) return -1;
        if (fstat(dir, &st) || st.st_uid || (!next && (st.st_mode & 077)) ||
            (next && (st.st_mode & 022) && !(st.st_mode & S_ISVTX))) { close(dir); errno = EPERM; return -1; }
        part = next;
    }
    if (dir < 0) return -1;
    int lock = openat(dir, "lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    struct stat st;
    if (lock < 0 || fstat(lock, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1 ||
        (st.st_mode & 077) || flock(lock, LOCK_EX | LOCK_NB)) {
        if (lock >= 0) close(lock);
        close(dir); return -1;
    }
    s->directory = dir; s->lock = lock; return 0;
}
void leon_boot_store_close(struct leon_boot_store *s) {
    if (s->lock >= 0) close(s->lock);
    if (s->directory >= 0) close(s->directory);
    *s = (struct leon_boot_store){-1, -1};
}
int leon_boot_state_load(struct leon_boot_store *store, struct leon_boot_state *s) {
    memset(s, 0, sizeof(*s));
    int fd = openat(store->directory, "state", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 1 : -1;
    char text[512] = {0}; struct stat st;
    ssize_t n = read(fd, text, sizeof(text) - 1);
    int bad = fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1 ||
              (st.st_mode & 077) || n <= 0 || n >= (ssize_t)sizeof(text) - 1;
    close(fd);
    if (bad || (size_t)n != strlen(text)) return -1;
    int used = 0, end = 0; unsigned crc;
    if (sscanf(text, "LEONBOOT2 %u %u %u %u %u %u %u %36s\n%n", &s->attempts, &s->failures,
        &s->pending, &s->mode, &s->next, &s->reason, &s->requested, s->boot_id, &used) != 8) {
        memset(s, 0, sizeof(*s)); used = end = 0;
        if (sscanf(text, "LEONBOOT1 %u %u %u %u %u %u %36s\n%n", &s->attempts, &s->failures,
            &s->pending, &s->mode, &s->next, &s->reason, s->boot_id, &used) != 7) return -1;
    }
    if (sscanf(text + used, "CRC32 %8x\n%n", &crc, &end) != 1 || used + end != n ||
        crc != checksum(text, (size_t)used) || !valid(s)) return -1;
    return 0;
}
int leon_boot_state_save(struct leon_boot_store *store, const struct leon_boot_state *s) {
    if (!valid(s)) { errno = EINVAL; return -1; }
    char text[512];
    int n = snprintf(text, sizeof(text), "LEONBOOT2 %u %u %u %u %u %u %u %s\n", s->attempts,
        s->failures, s->pending, s->mode, s->next, s->reason, s->requested, s->boot_id);
    unsigned crc = checksum(text, (size_t)n);
    n += snprintf(text + n, sizeof(text) - (size_t)n, "CRC32 %08x\n", crc);
    /* The lock serializes writers. Never follow a stale or malicious temp file. */
    if (unlinkat(store->directory, "state.next", 0) && errno != ENOENT) return -1;
    int fd = openat(store->directory, "state.next", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    int bad = write(fd, text, (size_t)n) != n || fsync(fd);
    close(fd);
    if (bad || renameat(store->directory, "state.next", store->directory, "state") || fsync(store->directory)) {
        unlinkat(store->directory, "state.next", 0); return -1;
    }
    return 0;
}
int leon_boot_begin(struct leon_boot_state *s, const char *id, unsigned requested) {
    if (!id || strlen(id) != 36 || requested > LEON_BOOT_RECOVERY) return -1;
    if (s->pending && s->failures < 1000000) ++s->failures;
    /* An explicit retry from Recovery must also work with a recovery archive.
     * Consume it once; an interrupted repair always takes precedence. */
    unsigned next = s->requested ? s->next : requested ? requested : s->next;
    if (s->requested && s->reason != LEON_BOOT_REPAIRING) s->failures = 0;
    s->requested = 0;
    s->next = LEON_BOOT_NORMAL;
    s->mode = next;
    if (s->reason == LEON_BOOT_REPAIRING || (s->failures >= 3 && !next)) s->mode = LEON_BOOT_RECOVERY;
    if (s->pending && s->reason == LEON_BOOT_OK) s->reason = LEON_BOOT_UNFINISHED;
    if (s->attempts < 1000000) ++s->attempts;
    s->pending = s->mode != LEON_BOOT_RECOVERY;
    memcpy(s->boot_id, id, 37);
    return valid(s) ? 0 : -1;
}
int leon_boot_success(struct leon_boot_state *s, const char *id) {
    if (!id || strcmp(s->boot_id, id) || !s->pending || s->mode == LEON_BOOT_RECOVERY) return -1;
    s->pending = s->failures = 0; s->reason = LEON_BOOT_OK;
    return 0;
}
int leon_boot_id(char id[37]) {
    int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, id, 36); close(fd); id[36] = 0;
    return n == 36 ? 0 : -1;
}
int leon_boot_publish(const struct leon_boot_state *s, const char *directory) {
    if (mkdir(directory, 0755) && errno != EEXIST) return -1;
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (dir < 0) return -1;
    if (fstat(dir, &st) || st.st_uid || (st.st_mode & 022)) { close(dir); return -1; }
    char text[512];
    int n = snprintf(text, sizeof(text), "mode=%s\nhealthy=%s\nattempts=%u\nfailures=%u\nreason=%s\nboot_id=%s\n",
        leon_boot_mode_name(s->mode), s->pending ? "false" : (s->mode == LEON_BOOT_RECOVERY ? "false" : "true"),
        s->attempts, s->failures, leon_boot_reason_name(s->reason), s->boot_id);
    unlinkat(dir, "status.next", 0);
    int fd = openat(dir, "status.next", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) { close(dir); return -1; }
    int bad = write(fd, text, (size_t)n) != n; close(fd);
    if (!bad) bad = renameat(dir, "status.next", dir, "status");
    close(dir); return bad ? -1 : 0;
}
int leon_boot_safe(void) {
    int fd = open("/run/leon-boot/status", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    char text[32] = {0};
    if (fd < 0) return 0;
    ssize_t n = read(fd, text, sizeof(text) - 1); close(fd);
    return n > 0 && !strncmp(text, "mode=safe\n", 10);
}
