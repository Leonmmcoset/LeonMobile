#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t action;
static void signal_handler(int sig) {
    if (sig != SIGCHLD)
        action = sig;
}
static void directory(const char *p, mode_t mode) {
    if (mkdir(p, mode) && errno != EEXIST)
        perror(p);
}
static void mount_fs(const char *src, const char *to, const char *type, unsigned long flags,
                     const char *data) {
    directory(to, 0755);
    if (mount(src, to, type, flags, data) && errno != EBUSY)
        perror(to);
}
static void boot_child(const char *path) {
    execl(path, path, (char *)NULL);
    perror(path);
    _exit(127);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (getpid() != 1) {
        fputs("leon-init must run as PID 1\n", stderr);
        return 1;
    }
    umask(022);
    mount_fs("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_fs("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_fs("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    directory("/dev/pts", 0755);
    mount_fs("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC,
             "mode=0620,ptmxmode=0666,gid=5");
    /* Optional tiny initramfs: the kernel command line names the real root. */
    if (access("/etc/leon/services", F_OK)) {
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
            perror("LeonMobile root");
            for (;;)
                pause();
        }
        if (mount("/proc", "/newroot/proc", NULL, MS_MOVE, NULL) ||
            mount("/sys", "/newroot/sys", NULL, MS_MOVE, NULL) ||
            mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL)) {
            perror("move early mounts");
            for (;;)
                pause();
        }
        execl("/bin/busybox", "busybox", "switch_root", "/newroot", "/sbin/leon-init",
              (char *)NULL);
        perror("switch_root");
        for (;;)
            pause();
    }
    mount_fs("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    mount_fs("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    mount_fs("/dev/vda2", "/data", "ext4", MS_NOSUID | MS_NODEV, NULL);
    directory("/data/var", 0755);
    if (mount("/data/var", "/var", NULL, MS_BIND, NULL))
        perror("/var");
    directory("/var/log", 0755);
    directory("/var/log/leon", 0750);
    directory("/run/leon", 0755);
    directory("/run/lock", 0755);
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("LANG", "C.UTF-8", 1);
    sethostname("leonmobile", 10);
    puts("LeonMobile 1.0.0: init ready");
    fflush(stdout);
    struct sigaction sa = {.sa_handler = signal_handler};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGPWR, &sa, NULL);
    pid_t manager = fork();
    if (manager == 0)
        boot_child("/sbin/leond");
    if (manager < 0)
        perror("leond fork");
    while (!action) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid == manager) {
            fputs("LeonMobile: service manager exited; restarting in 2 seconds\n", stderr);
            sleep(2);
            manager = fork();
            if (!manager)
                boot_child("/sbin/leond");
        } else if (pid < 0 && errno != EINTR)
            pause();
    }
    int how = (action == SIGINT || action == SIGUSR2) ? RB_AUTOBOOT : RB_POWER_OFF;
    if (manager > 0)
        kill(manager, SIGTERM);
    for (int i = 0; i < 60; ++i) {
        int status;
        if (waitpid(manager, &status, WNOHANG) == manager)
            break;
        usleep(100000);
    }
    kill(-1, SIGTERM);
    usleep(300000);
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    sync();
    umount2("/var", 0);
    umount2("/data", 0);
    mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL);
    reboot(how);
    for (;;)
        pause();
}
