#include "init.h"
#include "state.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
static int mount_wait(const char *device, const char *path, unsigned flags) {
    for (int i = 0; i < 100; ++i) {
        if (!mount(device, path, "ext4", flags, NULL)) return 0;
        if (errno != ENOENT && errno != ENXIO && errno != ENODEV) return -1;
        usleep(100000);
    }
    return -1;
}
_Noreturn void leon_early_recovery(unsigned reason) {
    char value[16]; snprintf(value, sizeof(value), "%u", reason);
    leon_boot_log("leon-init: entering independent recovery: %s\n", leon_boot_reason_name(reason));
    execl("/sbin/leon-recovery", "leon-recovery", "--reason", value, (char *)NULL);
    leon_boot_error("independent recovery exec");
    for (;;) pause();
}
void leon_boot_early(void) {
    char cmdline[2048] = {0}, id[37];
    FILE *f = fopen("/proc/cmdline", "r");
    if (f) { if (!fgets(cmdline, sizeof(cmdline), f)) cmdline[0] = 0; fclose(f); }
    /* This board's only repair targets are the fixed GPT system/data partitions. */
    if (!strstr(cmdline, "leon.root=/dev/vda1")) leon_early_recovery(LEON_BOOT_SYSTEM_MOUNT);
    mkdir("/run", 0755); mkdir("/data", 0755); mkdir("/tmp", 01777);
    if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755"))
        leon_early_recovery(LEON_BOOT_DATA);
    if (mount_wait("/dev/vda2", "/data", MS_NOSUID | MS_NODEV)) leon_early_recovery(LEON_BOOT_DATA);
    if (leon_boot_id(id)) leon_early_recovery(LEON_BOOT_CORRUPT);
    struct leon_boot_store store;
    struct leon_boot_state state;
    if (leon_boot_store_open(&store, "/data/leon/boot")) leon_early_recovery(LEON_BOOT_CORRUPT);
    int loaded = leon_boot_state_load(&store, &state);
    if (loaded < 0) { memset(&state, 0, sizeof(state)); state.reason = LEON_BOOT_CORRUPT; }
    unsigned requested = !access("/recovery-mode", F_OK) || strstr(cmdline, "leon.recovery=1") || loaded < 0
                         ? LEON_BOOT_RECOVERY : strstr(cmdline, "leon.safe=1") ? LEON_BOOT_SAFE : 0;
    if (leon_boot_begin(&state, id, requested) || leon_boot_state_save(&store, &state)) {
        leon_boot_store_close(&store); leon_early_recovery(LEON_BOOT_CORRUPT);
    }
    leon_boot_store_close(&store);
    if (leon_boot_publish(&state, "/run/leon-boot")) leon_early_recovery(LEON_BOOT_DATA);
    if (state.mode == LEON_BOOT_RECOVERY) leon_early_recovery(state.reason ? state.reason : LEON_BOOT_REQUESTED);
    leon_boot_log("leon-init: boot attempt=%u failures=%u mode=%s\n", state.attempts, state.failures,
                  leon_boot_mode_name(state.mode));
    if (mount_wait("/dev/vda1", "/newroot", MS_RDONLY)) leon_early_recovery(LEON_BOOT_SYSTEM_MOUNT);
    const char *required[] = {"/usr/sbin/leon-init", "/usr/sbin/leond", "/usr/bin/leon-compositor",
        "/usr/bin/leon-shell", "/usr/sbin/authd", "/etc/leon/services/00-prepare.toml", NULL};
    for (int i = 0; required[i]; ++i) {
        char path[256]; snprintf(path, sizeof(path), "/newroot%s", required[i]);
        if (access(path, R_OK)) { umount2("/newroot", 0); leon_early_recovery(LEON_BOOT_MISSING); }
    }
    if (mount("/proc", "/newroot/proc", NULL, MS_MOVE, NULL) ||
        mount("/sys", "/newroot/sys", NULL, MS_MOVE, NULL) ||
        mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL) ||
        mount("/data", "/newroot/data", NULL, MS_MOVE, NULL) ||
        mount("/run", "/newroot/run", NULL, MS_MOVE, NULL)) {
        leon_boot_error("move early mounts"); for (;;) pause();
    }
    leon_boot_log("leon-init: switching to the system filesystem\n");
    execl("/bin/busybox", "busybox", "switch_root", "/newroot", "/sbin/leon-init", (char *)NULL);
    leon_boot_error("switch_root"); for (;;) pause();
}
