#include "recovery.h"
#include "../rootd/rootd.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
int recovery_authenticate(struct recovery *r) {
    struct root_options o = {.directory = "/data/leon/rootd"};
    struct root_policy policy;
    int lock = root_policy_lock(&o), result = -1;
    if (lock < 0) return -1;
    if (!root_policy_load(&o, &policy) && policy.present && policy.enabled) {
        recovery_draw(r, "Enter Root password (hidden); Esc cancels");
        char password[129];
        if (recovery_line(r, password, sizeof(password), 1) > 0)
            result = root_authenticate(&o, &policy, password);
        root_clear(password, sizeof(password));
    }
    root_clear(&policy, sizeof(policy)); close(lock);
    if (result) snprintf(r->message, sizeof(r->message), "Root authentication unavailable/failed or delayed.");
    return result;
}
static int report(int fd, struct recovery *r) {
    return dprintf(fd, "LeonMobile Recovery 1.0.0\nreason=%s\nattempts=%u\nfailures=%u\nmode=%s\nboot_id=%s\ndata_available=%d\n",
        leon_boot_reason_name(r->reason), r->state.attempts, r->state.failures,
        leon_boot_mode_name(r->state.mode), r->state.boot_id, r->data_available) < 0 ? -1 : 0;
}
static void tail_file(struct recovery *r, const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    char text[2048]; ssize_t n = read(fd, text, sizeof(text)); close(fd);
    if (n <= 0) return;
    /* File content is displayed as text, never terminal escape sequences. */
    for (ssize_t i = 0; i < n; ++i) if ((unsigned char)text[i] < 32 && text[i] != '\n' && text[i] != '\t') text[i] = '?';
    for (int i = 0; i < 2; ++i) if (r->console[i] >= 0) (void)write(r->console[i], text, (size_t)n);
}
static int section(int out, int directory, const char *name, size_t limit) {
    int fd = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1 ||
        (st.st_mode & 077) || st.st_size < 0 || (uint64_t)st.st_size > limit) { close(fd); return -1; }
    int result = dprintf(out, "\n--- %s ---\n", name) < 0 ? -1 : 0;
    char buffer[4096]; ssize_t n;
    while (!result && (n = read(fd, buffer, sizeof(buffer))) != 0) {
        if (n < 0) { if (errno == EINTR) continue; result = -1; break; }
        if (write(out, buffer, (size_t)n) != n) result = -1;
    }
    close(fd); return result;
}
static int saved_logs(int fd) {
    int dir = open("/data/leon/logs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir >= 0) {
        for (int i = 0; i < 4; ++i) {
            char name[32]; snprintf(name, sizeof(name), "journal.%d", i);
            if (section(fd, dir, name, 2 * 1024 * 1024)) { close(dir); return -1; }
        }
        close(dir);
    } else if (errno != ENOENT) return -1;
    dir = open("/data/leon/crashes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir < 0) return errno == ENOENT ? 0 : -1;
    int result = section(fd, dir, "exits.json", 256 * 1024);
    DIR *list = fdopendir(dir);
    if (!list) { close(dir); return -1; }
    struct dirent *entry; int count = 0;
    while (!result && (entry = readdir(list)) && count < 32) {
        if (strncmp(entry->d_name, "report-", 7)) continue;
        int report_dir = openat(dir, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (report_dir < 0) { result = -1; break; }
        if (dprintf(fd, "\nCrash %s\n", entry->d_name) < 0 || section(fd, report_dir, "report.json", 65536)) result = -1;
        close(report_dir); ++count;
    }
    closedir(list); return result;
}
int recovery_report(struct recovery *r, int export) {
    if (!export) {
        recovery_draw(r, "Boot diagnostics (no private application data)");
        for (int i = 0; i < 2; ++i) if (r->console[i] >= 0) report(r->console[i], r);
        tail_file(r, "/tmp/recovery-check.log");
        char line[16]; recovery_line(r, line, sizeof(line), 0); return 0;
    }
    if (!r->data_available || recovery_authenticate(r)) return -1;
    struct leon_boot_store store;
    if (leon_boot_store_open(&store, "/data/leon/boot")) return -1;
    unlinkat(store.directory, "recovery-report.next", 0);
    int fd = openat(store.directory, "recovery-report.next", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    int result = -1;
    if (fd >= 0) {
        result = report(fd, r);
        char text[65536]; int n = klogctl(3, text, sizeof(text));
        if (n > 0 && write(fd, text, (size_t)n) != n) result = -1;
        if (saved_logs(fd) || fsync(fd)) result = -1;
        close(fd);
        if (!result && (renameat(store.directory, "recovery-report.next", store.directory, "recovery-report.txt") || fsync(store.directory))) result = -1;
    }
    if (result) unlinkat(store.directory, "recovery-report.next", 0);
    leon_boot_store_close(&store);
    snprintf(r->message, sizeof(r->message), "%s", result ? "Diagnostic export failed." :
        "Saved /data/leon/boot/recovery-report.txt (Root only).");
    return result;
}
