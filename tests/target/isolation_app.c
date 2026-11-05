#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

/* This ELF is packaged twice and launched by the real production app broker. */
#define CHECK(c) do { if (!(c)) { fprintf(result, "FAIL line=%d %s errno=%d\n", __LINE__, #c, errno); return 1; } } while (0)
static int write_file(const char *path, const char *text) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
    int valid = fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    if (fd >= 0) close(fd);
    return valid;
}
static int process_count(void) {
    DIR *d = opendir("/proc");
    if (!d) return -1;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)))
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') ++count;
    closedir(d);
    return count;
}
int main(int argc, char **argv) {
    (void)argc;
    if (!getenv("HOME") || chdir(getenv("HOME"))) return 2;
    FILE *result = fopen("isolation-result", "w");
    if (!result) return 2;
    setbuf(result, NULL);
    CHECK(getpid() == 2 && getppid() == 1 && getuid() >= 12000 && getuid() == geteuid());
    CHECK(!strcmp(getenv("TMPDIR"), "/tmp"));
    DIR *descriptors = opendir("/proc/self/fd");
    CHECK(descriptors);
    struct dirent *entry;
    while ((entry = readdir(descriptors))) {
        int fd = atoi(entry->d_name);
        CHECK(fd < 3 || fd == dirfd(descriptors) || fd == fileno(result));
    }
    closedir(descriptors);
    char text[4096] = {0};
    int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0 && read(fd, text, sizeof(text) - 1) > 0);
    close(fd);
    CHECK(strstr(text, "CapEff:\t0000000000000000") && strstr(text, "NoNewPrivs:\t1") &&
          strstr(text, "Seccomp:\t2"));
    CHECK(process_count() == 2);
    CHECK(access("/proc/1/root", R_OK) < 0 && !opendir("/proc/1/fd"));
    CHECK(access("/data/leon/shared", F_OK) < 0 && access("/data/leon/system", F_OK) < 0 &&
          access("/run/leon-sandbox", F_OK) < 0);
    CHECK(access("/tmp/leon-host-witness", F_OK) < 0 && errno == ENOENT);
    const char *id = getenv("LEON_APP_ID");
    const char *other = !strcmp(id, "org.example.isolationa") ? "org.example.isolationb" : "org.example.isolationa";
    char path[256];
    snprintf(path, sizeof(path), "/data/leon/data/%s", other);
    CHECK(access(path, F_OK) < 0);
    snprintf(path, sizeof(path), "/data/leon/apps/%s", other);
    CHECK(access(path, F_OK) < 0);
    CHECK(access(argv[0], R_OK | X_OK) == 0 && access("/etc/passwd", R_OK) == 0);
    CHECK(access("/etc/resolv.conf", R_OK) == 0 && access("/etc/localtime", R_OK) == 0);
    CHECK(open("/usr/namespace-write", O_CREAT | O_WRONLY, 0600) < 0);
    CHECK(open("/proc/sys/kernel/hostname", O_WRONLY) < 0);
    CHECK(access("/sys/fs/cgroup", F_OK) < 0);
    const char *private[] = {"/tmp/leon-private", "/var/tmp/leon-private", "/dev/shm/leon-private"};
    for (size_t i = 0; i < sizeof(private) / sizeof(private[0]); ++i) {
        CHECK(access(private[i], F_OK) < 0 && errno == ENOENT);
        CHECK(write_file(private[i], id));
    }
    struct stat st;
    CHECK(!stat("/dev/null", &st) && S_ISCHR(st.st_mode) && st.st_rdev == makedev(1, 3));
    const char *hardware[] = {"/dev/mem", "/dev/kmsg", "/dev/console", "/dev/vda", "/dev/input", "/dev/dri", "/dev/snd"};
    for (size_t i = 0; i < sizeof(hardware) / sizeof(hardware[0]); ++i)
        CHECK(open(hardware[i], O_RDONLY | O_CLOEXEC) < 0 && errno == ENOENT);
    CHECK(mknod("device-escape", S_IFCHR | 0600, makedev(1, 3)) < 0 && errno == EPERM);
    CHECK(unshare(CLONE_NEWUSER) < 0 && errno == EPERM);
    CHECK(mount("tmpfs", "/tmp", "tmpfs", 0, NULL) < 0 && errno == EPERM);
    CHECK(msgget(0x4c4d4953, 0) < 0 && errno == ENOENT);
    int queue = msgget(IPC_PRIVATE, 0600);
    CHECK(queue >= 0 && !msgctl(queue, IPC_RMID, NULL));
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    CHECK(master >= 0 && !grantpt(master) && !unlockpt(master));
    CHECK(!ptsname_r(master, path, sizeof(path)) && !strcmp(path, "/dev/pts/0"));
    int slave = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    CHECK(slave >= 0 && isatty(slave));
    close(slave); close(master);
    /* A private instance also needs a quota on the kernel's shared PTY pool. */
    int ptys[65], allocated = 0;
    while (allocated < 65) {
        int pty = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (pty < 0) break;
        ptys[allocated++] = pty;
    }
    /* musl maps the kernel's ENOSPC to the POSIX EAGAIN for posix_openpt. */
    CHECK(allocated == 64 && errno == EAGAIN);
    CHECK(open("/dev/ptmx", O_RDWR | O_NOCTTY | O_CLOEXEC) < 0 && errno == ENOSPC);
    for (int i = 0; i < allocated; ++i) close(ptys[i]);
    master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    CHECK(master >= 0 && !unlockpt(master) && !ptsname_r(master, path, sizeof(path)) &&
          !strcmp(path, "/dev/pts/0"));
    close(master);
    /* A short-lived detached orphan must be reaped by the app's own PID 1. */
    pid_t short_child = fork();
    CHECK(short_child >= 0);
    if (!short_child) {
        pid_t orphan = fork();
        if (orphan < 0) _exit(2);
        if (!orphan) { setsid(); usleep(100000); _exit(0); }
        _exit(0);
    }
    int status;
    CHECK(waitpid(short_child, &status, 0) == short_child && WIFEXITED(status) && !WEXITSTATUS(status));
    for (int i = 0; i < 100 && process_count() != 2; ++i) usleep(10000);
    CHECK(process_count() == 2);
    fprintf(result, "PASS private mounts, PID/IPC namespaces, devices, PTY, fd hygiene and orphan reaping\n");
    CHECK(write_file("isolation-ready", "ready"));
    for (;;) {
        char command[64] = {0};
        fd = open("isolation-command", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) { (void)read(fd, command, sizeof(command) - 1); close(fd); }
        if (!strcmp(command, "exit37")) return 37;
        if (!strcmp(command, "term")) raise(SIGTERM);
        if (!strcmp(command, "crash")) raise(SIGSEGV);
        if (!strcmp(command, "stubborn")) {
            signal(SIGTERM, SIG_IGN);
            CHECK(write_file("isolation-stubborn", "ready"));
            for (;;) pause();
        }
        if (!strcmp(command, "descendant")) {
            pid_t child = fork();
            CHECK(child >= 0);
            if (!child) { setsid(); for (;;) pause(); }
            CHECK(write_file("isolation-descendant", "ready"));
            return 42;
        }
        if (!strcmp(command, "timezone")) {
            ssize_t length = readlink("/run/leon-system/localtime", text, sizeof(text) - 1);
            CHECK(length > 0); text[length] = 0;
            CHECK(strstr(text, "Asia/Shanghai"));
            CHECK(!unlink("isolation-command"));
            CHECK(write_file("isolation-timezone", "ready"));
        }
        usleep(10000);
    }
}
