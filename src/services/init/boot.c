#include "init.h"
#include <errno.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
static void directory(const char *p, mode_t mode) {
    if (mkdir(p, mode) && errno != EEXIST)
        leon_boot_error(p);
}
static void mount_fs(const char *src, const char *to, const char *type, unsigned long flags,
                     const char *data) {
    directory(to, 0755);
    if (mount(src, to, type, flags, data) && errno != EBUSY)
        leon_boot_error(to);
}
static int mounted_type(const char *path, const char *type) {
    FILE *mounts = setmntent("/proc/mounts", "r");
    struct mntent *entry;
    int found = 0;
    while (mounts && (entry = getmntent(mounts)))
        if (!strcmp(entry->mnt_dir, path) && !strcmp(entry->mnt_type, type)) found = 1;
    if (mounts) endmntent(mounts);
    return found;
}
void leon_boot_prepare(void) {
    umask(022);
    mount_fs("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_fs("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_fs("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    directory("/dev/pts", 0755);
    mount_fs("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC,
             "mode=0620,ptmxmode=0666,gid=5");
    if (access("/etc/leon/services", F_OK))
        leon_boot_early();
    leon_boot_log("leon-init: mounting runtime and persistent filesystems\n");
    /* Early init carries boot status across switch_root on this mount. */
    if (!mounted_type("/run", "tmpfs"))
        mount_fs("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    mount_fs("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    mount_fs("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    mount_fs("/dev/vda2", "/data", "ext4", MS_NOSUID | MS_NODEV, NULL);
    directory("/data/var", 0755);
    if (mount("/data/var", "/var", NULL, MS_BIND, NULL))
        leon_boot_error("/var");
    directory("/var/log", 0755);
    directory("/var/log/leon", 0750);
    directory("/run/leon", 0755);
    directory("/run/lock", 0755);
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("LANG", "C.UTF-8", 1);
    sethostname("leonmobile", 10);
    leon_boot_log("LeonMobile 1.0.0: init ready\n");
}
