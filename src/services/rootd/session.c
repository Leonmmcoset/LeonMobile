#include "rootd.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define SESSION_ROOT "/sys/fs/cgroup/leon/root"
static int group_create(const struct root_options *o) {
    if (o->test)
        return 0;
    char path[PATH_MAX];
    if (lm_mkdir(SESSION_ROOT, 0700))
        return -1;
    snprintf(path, sizeof(path), SESSION_ROOT "/s%d", getpid());
    if (mkdir(path, 0700))
        return -1;
    snprintf(path, sizeof(path), SESSION_ROOT "/s%d/cgroup.procs", getpid());
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int result = write(fd, "0", 1) == 1 ? 0 : -1;
    close(fd);
    return result;
}
static void group_kill(pid_t worker) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), SESSION_ROOT "/s%d/cgroup.kill", worker);
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)write(fd, "1", 1);
        close(fd);
    }
    snprintf(path, sizeof(path), SESSION_ROOT "/s%d", worker);
    rmdir(path);
}
void root_session_cleanup(const struct root_options *o, pid_t worker) {
    if (o->test)
        return;
    if (worker) {
        group_kill(worker);
        return;
    }
    DIR *directory = opendir(SESSION_ROOT);
    if (!directory)
        return;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        char tail;
        int pid;
        if (sscanf(entry->d_name, "s%d%c", &pid, &tail) == 1 && pid > 1)
            group_kill(pid);
    }
    closedir(directory);
}
int root_session(const struct root_options *o, int socket, int notice, struct ucred peer,
                 const lm_json *request, int64_t generation) {
    int64_t rows = lm_integer(lm_get(request, "rows"), 24),
            columns = lm_integer(lm_get(request, "columns"), 80);
    const char *command = lm_string(lm_get(request, "command"), "");
    if (rows < 2 || rows > 512 || columns < 8 || columns > 1024 || strlen(command) > 4096) {
        root_reply(socket, lm_failure("invalid root terminal size or command"), -1);
        return -1;
    }
    int lock = root_policy_lock(o);
    struct root_policy policy;
    if (lock < 0 || root_policy_load(o, &policy) || !policy.enabled ||
        policy.generation != generation || group_create(o)) {
        if (lock >= 0)
            close(lock);
        root_reply(socket, lm_failure("root access was revoked or session isolation failed"), -1);
        return -1;
    }
    const char *home = o->test ? o->directory : "/data/home/root";
    struct stat home_info;
    if (lm_mkdir(home, 0700) || lstat(home, &home_info) || home_info.st_uid ||
        (home_info.st_mode & 077)) {
        close(lock);
        root_reply(socket, lm_failure("root home unavailable"), -1);
        return -1;
    }
    int master = -1;
    struct winsize size = {(unsigned short)rows, (unsigned short)columns, 0, 0};
    pid_t parent = getpid(), child = forkpty(&master, NULL, NULL, &size);
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent || setgroups(0, NULL) ||
            setgid(0) || setuid(0) || chdir(home))
            _exit(126);
        if (syscall(SYS_close_range, 3, ~0U, 0))
            _exit(126);
        clearenv();
        setenv("HOME", home, 1);
        setenv("USER", "root", 1);
        setenv("LOGNAME", "root", 1);
        setenv("SHELL", "/bin/sh", 1);
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("PS1", "root@leonmobile:\\w# ", 1);
        if (*command)
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        else
            execl("/bin/sh", "-sh", "-i", (char *)NULL);
        _exit(127);
    }
    close(lock);
    if (child < 0) {
        root_reply(socket, lm_failure("cannot create root PTY"), -1);
        return -1;
    }
    struct root_session_notice event = {generation};
    if (write(notice, &event, sizeof(event)) != sizeof(event)) {
        kill(-child, SIGKILL);
        kill(child, SIGKILL);
    } else {
        lm_json *reply = lm_success();
        lm_set_integer(reply, "pid", child);
        lm_set_integer(reply, "uid", 0);
        fprintf(stderr, "rootd: session started caller_uid=%u caller_pid=%d child=%d\n", peer.uid,
                peer.pid, child);
        if (root_reply(socket, reply, master)) {
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
        }
    }
    int status = 0;
    for (;;) {
        pid_t reaped = waitpid(child, &status, WNOHANG);
        if (reaped == child || (reaped < 0 && errno != EINTR))
            break;
        struct pollfd connection = {socket, POLLIN, 0};
        if (poll(&connection, 1, 50) > 0) {
            char byte;
            ssize_t count = recv(socket, &byte, 1, MSG_DONTWAIT);
            if (count == 0 || count > 0 || (count < 0 && errno != EINTR && errno != EAGAIN)) {
                kill(-child, SIGKILL);
                kill(child, SIGKILL);
                while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
                }
                break;
            }
        }
    }
    close(master);
    lm_json *reply = lm_success();
    lm_set_integer(reply, "status",
                   WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    root_reply(socket, reply, -1);
    return 0;
}
