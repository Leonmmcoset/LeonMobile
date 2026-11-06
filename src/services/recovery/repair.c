#include "recovery.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static int expected(char hash[65], uint64_t *size) {
    FILE *f = fopen("/etc/leon-recovery/system.sha256", "r");
    if (!f) return -1;
    int n = fscanf(f, "%64s", hash); fclose(f);
    f = fopen("/etc/leon-recovery/system.size", "r");
    if (!f || n != 1) { if (f) fclose(f); return -1; }
    unsigned long long value = 0;
    n = fscanf(f, "%llu", &value); fclose(f); *size = value;
    return n == 1 && value >= 4096 ? 0 : -1;
}
static int partition(void) {
    char value[32] = {0};
    int fd = open("/sys/class/block/vda1/partition", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, value, sizeof(value) - 1); close(fd);
    return n > 0 && !strcmp(value, "1\n") ? 0 : -1;
}
int recovery_restore(struct recovery *r) {
    int source = -1, target = -1, result = -1;
    char hash[65]; uint64_t size, source_size, target_size; struct stat a, b;
    if (!r->data_available || expected(hash, &size) || partition()) goto done;
    source = open("/dev/vdb", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int readonly = 0;
    if (source < 0 || fstat(source, &a) || !S_ISBLK(a.st_mode) ||
        ioctl(source, BLKROGET, &readonly) || !readonly || ioctl(source, BLKGETSIZE64, &source_size) ||
        source_size != size || recovery_verify(source, size, hash)) goto done;
    /* O_EXCL on a block device refuses a mounted system partition. */
    target = open("/dev/vda1", O_RDWR | O_EXCL | O_CLOEXEC | O_NOFOLLOW);
    if (target < 0 || fstat(target, &b) || !S_ISBLK(b.st_mode) || a.st_rdev == b.st_rdev ||
        ioctl(target, BLKGETSIZE64, &target_size) || size != target_size) goto done;
    r->state.reason = LEON_BOOT_REPAIRING;
    if (recovery_save(r)) goto done; /* Persist recovery before the first write. */
    snprintf(r->message, sizeof(r->message), "Restoring system; keep power connected.");
    recovery_draw(r, "Verified source; writing system only");
    unsigned char *buffer = malloc(1024 * 1024);
    if (!buffer) goto done;
    uint64_t offset = 0;
    while (offset < size) {
        size_t count = size - offset > 1024 * 1024 ? 1024 * 1024 : (size_t)(size - offset);
        ssize_t n = pread(source, buffer, count, (off_t)offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        size_t sent = 0;
        while (sent < (size_t)n) {
            ssize_t written = pwrite(target, buffer + sent, (size_t)n - sent, (off_t)(offset + sent));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) break;
            sent += (size_t)written;
        }
        if (sent != (size_t)n) break;
        offset += (uint64_t)n;
    }
    free(buffer);
    if (offset != size || fsync(target) || recovery_verify(target, size, hash)) goto done;
    r->state.reason = LEON_BOOT_REPAIRED; r->state.failures = r->state.pending = 0;
    if (recovery_save(r)) goto done;
    result = 0;
done:
    if (source >= 0) close(source);
    if (target >= 0) close(target);
    snprintf(r->message, sizeof(r->message), "%s", result ?
        "Restore refused/failed. Data retained; see diagnostics." : "System restored and read-back verified. Data retained.");
    recovery_draw(r, result ? "Restore refused or failed; data was not modified" :
                             "Verified restore complete; applications and data were preserved");
    return result;
}
int recovery_check(struct recovery *r) {
    int fd = open("/dev/vda1", O_RDONLY | O_EXCL | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || partition()) { if (fd >= 0) close(fd); return -1; }
    close(fd);
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) {
        int log = open("/tmp/recovery-check.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (log < 0) _exit(127);
        dup2(log, 1); dup2(log, 2); close(log);
        execl("/sbin/e2fsck", "e2fsck", "-f", "-n", "/dev/vda1", (char *)NULL); _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    int good = WIFEXITED(status) && !WEXITSTATUS(status);
    snprintf(r->message, sizeof(r->message), "%s", good ?
        "Filesystem check passed (read only)." : "Filesystem check found errors; restore is available.");
    return good ? 0 : -1;
}
