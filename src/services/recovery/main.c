#include "recovery.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
int recovery_save(struct recovery *r) {
    if (!r->data_available) return -1;
    struct leon_boot_store store;
    if (leon_boot_store_open(&store, "/data/leon/boot")) return -1;
    int bad = leon_boot_state_save(&store, &r->state);
    leon_boot_store_close(&store); return bad;
}
void recovery_reboot(struct recovery *r, unsigned mode, int poweroff) {
    if (!poweroff && r->data_available) {
        r->state.next = mode;
        r->state.requested = 1;
        if (r->state.reason != LEON_BOOT_REPAIRING) { r->state.failures = 0; r->state.reason = LEON_BOOT_OK; }
        r->state.pending = 0;
        if (recovery_save(r)) { snprintf(r->message, sizeof(r->message), "Unable to persist boot request."); return; }
    }
    sync(); umount2("/data", 0);
    reboot(poweroff ? RB_POWER_OFF : RB_AUTOBOOT);
    for (;;) pause();
}
int main(int argc, char **argv) {
    if (argc == 7 && !strcmp(argv[1], "--verify-image") && !strcmp(argv[3], "--sha256") && !strcmp(argv[5], "--size")) {
        char *end; unsigned long long size = strtoull(argv[6], &end, 10);
        if (*end) return 2;
        int fd = open(argv[2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat st;
        int good = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && (uint64_t)st.st_size == size &&
                   !recovery_verify(fd, size, argv[4]);
        if (fd >= 0) close(fd);
        puts(good ? "Image verified" : "Image rejected"); return good ? 0 : 1;
    }
    if (getpid() != 1 || geteuid()) { fputs("Recovery runs only as initramfs PID 1.\n", stderr); return 2; }
    struct recovery r = {.reason = LEON_BOOT_REQUESTED, .console = {-1, -1}};
    if (argc == 3 && !strcmp(argv[1], "--reason")) r.reason = (unsigned)strtoul(argv[2], NULL, 10);
    signal(SIGPIPE, SIG_IGN);
    /* No shell, network listener, system binaries or automatic destructive action. */
    mkdir("/tmp", 01777);
    mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=1777,size=8m");
    int mounted = 0;
    FILE *mounts = setmntent("/proc/mounts", "r");
    struct mntent *entry;
    while (mounts && (entry = getmntent(mounts)))
        if (!strcmp(entry->mnt_dir, "/data") && !strcmp(entry->mnt_fsname, "/dev/vda2") &&
            !strcmp(entry->mnt_type, "ext4") && hasmntopt(entry, "rw")) mounted = 1;
    if (mounts) endmntent(mounts);
    if (mounted) {
        struct leon_boot_store store;
        if (!leon_boot_store_open(&store, "/data/leon/boot")) {
            if (!leon_boot_state_load(&store, &r.state)) r.data_available = 1;
            leon_boot_store_close(&store);
        }
    }
    if (r.data_available) { r.state.mode = LEON_BOOT_RECOVERY; r.state.reason = r.reason; recovery_save(&r); }
    if (recovery_console_open(&r)) for (;;) pause();
    for (;;) {
        recovery_draw(&r, "System repair and startup options");
        char choice[64];
        if (recovery_line(&r, choice, sizeof(choice), 0) < 0) continue;
        if (!strcmp(choice, "1")) recovery_reboot(&r, LEON_BOOT_NORMAL, 0);
        else if (!strcmp(choice, "2")) recovery_reboot(&r, LEON_BOOT_SAFE, 0);
        else if (!strcmp(choice, "3")) recovery_check(&r);
        else if (!strcmp(choice, "4")) {
            recovery_draw(&r, "Type RESTORE to replace system; data is retained");
            if (recovery_line(&r, choice, sizeof(choice), 0) > 0 && !strcmp(choice, "RESTORE")) recovery_restore(&r);
            else snprintf(r.message, sizeof(r.message), "Restore cancelled; no system write.");
        } else if (!strcmp(choice, "5")) recovery_report(&r, 0);
        else if (!strcmp(choice, "6")) recovery_report(&r, 1);
        else if (!strcmp(choice, "7")) recovery_reboot(&r, 0, 1);
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }
}
