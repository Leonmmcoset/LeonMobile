#include "init.h"
#include <errno.h>
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
void leon_boot_prepare(void) {
    umask(022);
    mount_fs("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_fs("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_fs("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    directory("/dev/pts", 0755);
    mount_fs("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC,
             "mode=0620,ptmxmode=0666,gid=5");
    /* Optional tiny initramfs: the kernel command line names the real root. */
    if (access("/etc/leon/services", F_OK)) {
        leon_boot_log("leon-init: early userspace ready\n");
        char cmdline[2048] = {0}, root[256] = "/dev/vda1";
        FILE *f = fopen("/proc/cmdline", "r");
        if (f) {
            if (!fgets(cmdline, sizeof(cmdline), f))
                cmdline[0] = 0;
            fclose(f);
        }
        char *p = strstr(cmdline, "leon.root=");
        if (p)
            sscanf(p + 10, "%255s", root);
        leon_boot_log("leon-init: mounting %s as read-only ext4\n", root);
        directory("/newroot", 0755);
        int mounted = 0;
        for (int i = 0; i < 100; ++i) {
            if (!mount(root, "/newroot", "ext4", MS_RDONLY, NULL)) {
                mounted = 1;
                break;
            }
            usleep(100000);
        }
        if (!mounted) {
            leon_boot_error("LeonMobile root");
            for (;;)
                pause();
        }
        if (mount("/proc", "/newroot/proc", NULL, MS_MOVE, NULL) ||
            mount("/sys", "/newroot/sys", NULL, MS_MOVE, NULL) ||
            mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL)) {
            leon_boot_error("move early mounts");
            for (;;)
                pause();
        }
        leon_boot_log("leon-init: switching to the system filesystem\n");
        execl("/bin/busybox", "busybox", "switch_root", "/newroot", "/sbin/leon-init",
              (char *)NULL);
        leon_boot_error("switch_root");
        for (;;)
            pause();
    }
    leon_boot_log("leon-init: mounting runtime and persistent filesystems\n");
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
