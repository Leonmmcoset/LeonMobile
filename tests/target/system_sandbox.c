#define _GNU_SOURCE
#include "../../src/services/appd/sandbox.h"
#include "system_probe.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
static const char *home = "/data/leon/data/org.leon.gallery", *runtime = "/run/leon/apps/11002";
static int child(int network) {
    if (setgroups(0, NULL) || setgid(11002) || setuid(11002) ||
        leon_sandbox(home, runtime, network, 0))
        return 10;
    int fd = open("/data/leon/shared/sandbox-public", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 11;
    }
    fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0)
        return 12;
    close(fd);
    char path[256];
    snprintf(path, sizeof(path), "%s/sandbox-private", home);
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0)
        return 13;
    close(fd);
    fd = open("/tmp/sandbox-escape", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
        close(fd);
        return 14;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 15;
    close(fd);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if ((fd >= 0) != network) {
        if (fd >= 0)
            close(fd);
        return 16;
    }
    if (fd >= 0)
        close(fd);
    fd = socket(AF_NETLINK, SOCK_RAW, 0);
    if (fd >= 0) {
        close(fd);
        return 17;
    }
    char ring[256] = {0};
    errno = 0;
    if (syscall(425, 1, ring) != -1 || errno != EPERM)
        return 19;
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) || limit.rlim_cur != 512)
        return 18;
    return 0;
}
static int run(int network, int oom, int processes) {
    int gate[2];
    if (pipe(gate))
        return 1;
    pid_t pid = fork();
    if (!pid) {
        close(gate[1]);
        char go;
        if (read(gate[0], &go, 1) != 1)
            _exit(99);
        close(gate[0]);
        setsid();
        int status = child(network);
        if (status)
            _exit(status);
        if (oom) {
            size_t size = 400 * 1024 * 1024;
            volatile char *memory = malloc(size);
            if (!memory)
                _exit(25);
            for (size_t i = 0; i < size; i += 4096)
                memory[i] = 1;
            _exit(26);
        }
        if (processes) {
            int count = 0;
            pid_t children[160];
            while (count < 160) {
                pid_t p = fork();
                if (p < 0)
                    break;
                if (!p) {
                    pause();
                    _exit(0);
                }
                children[count++] = p;
            }
            int valid = count > 0 && count < 128 && errno == EAGAIN;
            for (int i = 0; i < count; ++i)
                kill(children[i], SIGKILL);
            while (waitpid(-1, NULL, 0) > 0) {
            }
            _exit(valid ? 0 : 27);
        }
        _exit(0);
    }
    close(gate[0]);
    if (pid < 0 || leon_cgroup(pid, 11002)) {
        close(gate[1]);
        if (pid > 0) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
        return 2;
    }
    if (write(gate[1], "1", 1) != 1)
        return 3;
    close(gate[1]);
    int status;
    if (waitpid(pid, &status, 0) != pid)
        return 4;
    if (oom)
        return !(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "sandbox child network=%d processes=%d status=%d\n", network, processes,
                status);
        return 5;
    }
    return 0;
}
int probe_sandbox(void) {
    mkdir(home, 0700);
    mkdir("/run/leon/apps", 0755);
    mkdir(runtime, 0700);
    if (chown(home, 11002, 11002) || chown(runtime, 11002, 11002))
        return 1;
    int fd = open("/data/leon/shared/sandbox-public", O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0)
        return 2;
    close(fd);
    if (run(0, 0, 0) || run(1, 0, 0) || run(0, 0, 1) || run(0, 1, 0))
        return 3;
    // A real fatal signal must be collected even after the sandbox is applied.
    pid_t pid = fork();
    if (!pid) {
        if (setgroups(0, NULL) || setgid(11002) || setuid(11002) ||
            leon_sandbox(home, runtime, 0, 0) || prctl(PR_SET_DUMPABLE, 1))
            _exit(30);
        raise(SIGSEGV);
        _exit(31);
    }
    int status;
    if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFSIGNALED(status) ||
        WTERMSIG(status) != SIGSEGV)
        return 4;
    unlink("/data/leon/shared/sandbox-public");
    return 0;
}
