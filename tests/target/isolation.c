#define _GNU_SOURCE
#include "system_probe.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *report;
#define CHECK(c) do { if (!(c)) { fprintf(report, "FAIL isolation:%d %s errno=%d reply=%s\n", __LINE__, #c, errno, probe_reply()); return 1; } } while (0)
static const char *app[] = {"org.example.isolationa", "org.example.isolationb"};
static int rpc(const char *op, int index) {
    char request[512];
    snprintf(request, sizeof(request), "{\"op\":\"%s\",\"id\":\"%s\"}", op, app[index]);
    return !probe_rpc("apps", request, 0) && probe_has("\"ok\":true");
}
static int write_command(int index, const char *command) {
    char path[256];
    snprintf(path, sizeof(path), "/data/leon/data/%s/isolation-command", app[index]);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return 0;
    int valid = write(fd, command, strlen(command)) == (ssize_t)strlen(command);
    close(fd);
    return valid;
}
static int wait_file(int index, const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "/data/leon/data/%s/%s", app[index], name);
    for (int i = 0; i < 500; ++i) {
        if (!access(path, F_OK)) return 1;
        usleep(10000);
    }
    snprintf(path, sizeof(path), "/data/leon/data/%s/isolation-result", app[index]);
    FILE *result = fopen(path, "r");
    char text[512];
    if (result) { while (fgets(text, sizeof(text), result)) fputs(text, report); fclose(result); }
    return 0;
}
static int start(int index, pid_t *pid, uid_t *uid) {
    char path[256];
    const char *stale[] = {"isolation-ready", "isolation-stubborn", "isolation-descendant", "isolation-command", "isolation-result"};
    for (size_t i = 0; i < sizeof(stale) / sizeof(stale[0]); ++i) {
        snprintf(path, sizeof(path), "/data/leon/data/%s/%s", app[index], stale[i]);
        unlink(path);
    }
    CHECK(rpc("launch", index));
    *pid = (pid_t)probe_integer("pid");
    CHECK(*pid > 2 && wait_file(index, "isolation-ready"));
    snprintf(path, sizeof(path), "/data/leon/data/%s", app[index]);
    struct stat st;
    CHECK(!stat(path, &st));
    *uid = st.st_uid;
    return 0;
}
static int wait_exit(int index, pid_t pid, int status) {
    char expected[96];
    snprintf(expected, sizeof(expected), "\"pid\":%d,\"status\":%d", pid, status);
    for (int i = 0; i < 500; ++i) {
        CHECK(rpc("lifecycle", index));
        if (probe_has(expected) && kill(pid, 0) < 0 && errno == ESRCH) return 0;
        usleep(10000);
    }
    CHECK(0);
}
static int empty_group(uid_t uid) {
    char path[256], text[1024] = {0};
    snprintf(path, sizeof(path), "/sys/fs/cgroup/leon/apps/%u/cgroup.events", uid);
    for (int i = 0; i < 100; ++i) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return 0;
        ssize_t n = read(fd, text, sizeof(text) - 1);
        close(fd);
        if (n > 0) { text[n] = 0; if (strstr(text, "populated 0")) return 1; }
        usleep(10000);
    }
    return 0;
}
static int namespace_inode(pid_t pid, const char *name, ino_t *inode) {
    char path[128]; struct stat st;
    snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, name);
    if (stat(path, &st)) return 0;
    *inode = st.st_ino;
    return 1;
}
static pid_t parent(pid_t pid) {
    char path[128], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    pid_t value = -1;
    if (f) {
        while (fgets(line, sizeof(line), f)) if (sscanf(line, "PPid:%d", &value) == 1) break;
        fclose(f);
    }
    return value;
}
int probe_isolation(FILE *output) {
    report = output;
    int queue = msgget(0x4c4d4953, IPC_CREAT | IPC_EXCL | 0666);
    CHECK(queue >= 0);
    int host_pty = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    CHECK(host_pty >= 0 && !grantpt(host_pty) && !unlockpt(host_pty));
    int witness = open("/tmp/leon-host-witness", O_CREAT | O_WRONLY | O_EXCL | O_CLOEXEC, 0644);
    CHECK(witness >= 0);
    close(witness);
    CHECK(!probe_rpc("apps", "{\"op\":\"install\",\"file\":\"/usr/share/isolation-a.lmpkg\"}", 0) && probe_has("\"ok\":true"));
    CHECK(!probe_rpc("apps", "{\"op\":\"install\",\"file\":\"/usr/share/isolation-b.lmpkg\"}", 0) && probe_has("\"ok\":true"));
    pid_t a, b; uid_t ua, ub;
    CHECK(!start(0, &a, &ua) && !start(1, &b, &ub) && ua != ub);
    for (const char **name = (const char *[]){"mnt", "pid", "ipc", NULL}; *name; ++name) {
        ino_t host, first, second;
        CHECK(namespace_inode(getpid(), *name, &host) && namespace_inode(a, *name, &first) && namespace_inode(b, *name, &second));
        CHECK(first != host && second != host && first != second);
    }
    CHECK(access("/tmp/leon-private", F_OK) < 0 && access("/var/tmp/leon-private", F_OK) < 0 && access("/dev/shm/leon-private", F_OK) < 0);
    CHECK(!probe_rpc("time", "{\"op\":\"configure\",\"automatic\":false,\"timezone\":\"Asia/Shanghai\"}", 0) && probe_has("\"ok\":true"));
    CHECK(write_command(0, "timezone") && wait_file(0, "isolation-timezone"));
    CHECK(!probe_rpc("time", "{\"op\":\"configure\",\"automatic\":true,\"timezone\":\"Etc/UTC\"}", 0) && probe_has("\"ok\":true"));
    CHECK(write_command(0, "exit37") && !wait_exit(0, a, 37) && empty_group(ua));
    CHECK(!kill(b, 0));
    CHECK(write_command(1, "term") && !wait_exit(1, b, 143) && empty_group(ub));
    CHECK(!start(0, &a, &ua)); /* Scratch mounts and PTY numbering must start fresh. */
    CHECK(write_command(0, "descendant") && !wait_exit(0, a, 42) && empty_group(ua));
    CHECK(!start(0, &a, &ua));
    CHECK(write_command(0, "stubborn") && wait_file(0, "isolation-stubborn"));
    CHECK(rpc("stop", 0) && !wait_exit(0, a, 137) && empty_group(ua));
    CHECK(!start(0, &a, &ua));
    pid_t supervisor = parent(a);
    CHECK(supervisor > 1 && !kill(supervisor, SIGKILL));
    CHECK(!wait_exit(0, a, 137) && empty_group(ua));
    CHECK(!start(0, &a, &ua));
    CHECK(write_command(0, "crash") && !wait_exit(0, a, 139) && empty_group(ua));
    int captured = 0;
    char actual_pid[64]; snprintf(actual_pid, sizeof(actual_pid), "\"pid\":%d", a);
    for (int i = 0; i < 100; ++i) {
        CHECK(!probe_rpc("crash", "{\"op\":\"list\"}", 0) && probe_has("\"ok\":true"));
        if (probe_has(app[0]) && probe_has(actual_pid) && probe_has("\"signal\":11") && probe_integer("core_bytes") > 0) { captured = 1; break; }
        usleep(10000);
    }
    CHECK(captured);
    /* An invalid, privileged mount staging entry must fail before exec. */
    CHECK(!rmdir("/run/leon-sandbox") && !symlink("/data/leon/shared", "/run/leon-sandbox"));
    CHECK(!rpc("launch", 0) && probe_has("setup/exec failed"));
    CHECK(!unlink("/run/leon-sandbox") && !mkdir("/run/leon-sandbox", 0700) && empty_group(ua));
    char home[256], backup[256];
    snprintf(home, sizeof(home), "/data/leon/data/%s", app[0]);
    snprintf(backup, sizeof(backup), "/data/leon/data/%s.saved", app[0]);
    struct stat before, after;
    CHECK(!stat("/data/leon/shared", &before) && !rename(home, backup) && !symlink("/data/leon/shared", home));
    CHECK(!rpc("launch", 0));
    CHECK(!stat("/data/leon/shared", &after) && before.st_uid == after.st_uid && before.st_mode == after.st_mode);
    CHECK(!unlink(home) && !rename(backup, home));
    for (int i = 0; i < 4; ++i) {
        CHECK(!start(0, &a, &ua));
        CHECK(write_command(0, "exit37") && !wait_exit(0, a, 37) && empty_group(ua));
    }
    CHECK(!start(0, &a, &ua));
    supervisor = parent(a);
    pid_t broker = parent(supervisor);
    CHECK(supervisor > 1 && broker > 1 && !kill(broker, SIGKILL));
    int recovered = 0;
    for (int i = 0; i < 1000; ++i) {
        if (rpc("lifecycle", 0) && !probe_has(app[0]) && kill(a, 0) < 0 && errno == ESRCH) {
            recovered = 1;
            break;
        }
        usleep(10000);
    }
    CHECK(recovered && empty_group(ua));
    CHECK(!start(0, &a, &ua));
    CHECK(write_command(0, "exit37") && !wait_exit(0, a, 37) && empty_group(ua));
    CHECK(rpc("remove", 0) && rpc("remove", 1));
    CHECK(!msgctl(queue, IPC_RMID, NULL));
    close(host_pty);
    CHECK(!unlink("/tmp/leon-host-witness"));
    fputs("PASS per-app mount/PID/IPC namespaces, private scratch/devpts, device denial, actual host PID, reaping, signals/core, forced stop, init/broker death, fail-closed mounts/symlinks and relaunch\n", report);
    return 0;
}
