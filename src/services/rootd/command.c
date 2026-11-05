#include "apps.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int valid(const struct root_options *o, int socket, const lm_json *r) {
    char byte;
    ssize_t n = recv(socket, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
    if (!(n < 0 && (errno == EAGAIN || errno == EINTR)))
        return 0;
    struct root_policy policy;
    pid_t ui = lm_integer(lm_get(r, "ui_pid"), -1);
    return !root_policy_load(o, &policy) && policy.enabled &&
           policy.generation == lm_integer(lm_get(r, "generation"), -1) &&
           root_process_start(ui) == lm_integer(lm_get(r, "ui_start"), -2) &&
           root_apps_current(o, r) && root_apps_revision(o, r);
}
int root_command(const struct root_options *o, int socket, int helper, const lm_json *r) {
    unsigned char header[64];
    if (pread(helper, header, sizeof(header), 0) != sizeof(header) ||
        memcmp(header, "\177ELF", 4) || header[4] != 2 || header[5] != 1 ||
        (header[16] != 2 && header[16] != 3) ||
        (!o->test && (header[18] != 183 || header[19] != 0))) {
        root_reply(socket, lm_failure("Root helper is not a compatible ELF"), -1);
        return -1;
    }
    int lock = root_apps_lock(o);
    char name[64];
    root_apps_name(name, sizeof(name), getpid());
    lm_json *record = lm_copy(r);
    lm_set_string(record, "state", "running");
    int failed =
        lock < 0 || root_apps_audit(o, r, "started", -1) || root_apps_write(o, name, record);
    lm_free(record);
    if (lock >= 0)
        close(lock);
    if (failed || !valid(o, socket, r)) {
        root_reply(socket, lm_failure("Root request was revoked before execution"), -1);
        return -1;
    }
    int output[2];
    if (pipe2(output, O_CLOEXEC)) {
        root_reply(socket, lm_failure("cannot create Root output channel"), -1);
        return -1;
    }
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent || setsid() < 0 ||
            setgroups(0, NULL) || setgid(0) || setuid(0) || chdir("/"))
            _exit(126);
        int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (input < 0 || dup2(input, 0) < 0 || dup2(output[1], 1) < 0 || dup2(output[1], 2) < 0 ||
            (helper != 3 && dup3(helper, 3, O_CLOEXEC) < 0) || syscall(SYS_close_range, 4, ~0U, 0))
            _exit(126);
        struct rlimit cpu = {30, 30}, files = {64, 64}, core = {0, 0};
        if (setrlimit(RLIMIT_CPU, &cpu) || setrlimit(RLIMIT_NOFILE, &files) ||
            setrlimit(RLIMIT_CORE, &core) || prctl(PR_SET_DUMPABLE, 0))
            _exit(126);
        clearenv();
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("HOME", "/", 1);
        setenv("USER", "root", 1);
        setenv("LOGNAME", "root", 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("LEON_CALLER_APP", lm_string(lm_get(r, "id"), ""), 1);
        char uid[32];
        snprintf(uid, sizeof(uid), "%lld", (long long)lm_integer(lm_get(r, "uid"), -1));
        setenv("LEON_CALLER_UID", uid, 1);
        const lm_json *args = lm_get(r, "args");
        char *argv[6] = {(char *)lm_string(lm_get(r, "entry"), "root-helper"), NULL};
        for (size_t i = 0; i < lm_size(args); ++i)
            argv[i + 1] = (char *)lm_string(lm_at(args, i), "");
        syscall(SYS_execveat, 3, "", argv, environ, AT_EMPTY_PATH);
        _exit(127);
    }
    close(output[1]);
    if (child < 0) {
        close(output[0]);
        root_reply(socket, lm_failure("cannot create Root helper"), -1);
        return -1;
    }
    int flags = fcntl(output[0], F_GETFL);
    if (flags < 0 || fcntl(output[0], F_SETFL, flags | O_NONBLOCK))
        failed = 1;
    lm_json *started = lm_success();
    lm_set_string(started, "state", "running");
    lm_set_integer(started, "pid", child);
    if (root_reply(socket, started, -1))
        failed = 1;
    int status = 0, exited = 0, eof = 0;
    size_t total = 0;
    int64_t end = lm_now_ms() + 30000;
    while (!failed && (!exited || !eof)) {
        if (lm_now_ms() > end || !valid(o, socket, r)) {
            failed = 1;
            break;
        }
        unsigned char bytes[512];
        ssize_t count = read(output[0], bytes, sizeof(bytes));
        if (count > 0) {
            total += (size_t)count;
            if (total > 65536) {
                failed = 1;
                break;
            }
            char encoded[1025];
            for (ssize_t i = 0; i < count; ++i) {
                encoded[i * 2] = "0123456789abcdef"[bytes[i] >> 4];
                encoded[i * 2 + 1] = "0123456789abcdef"[bytes[i] & 15];
            }
            encoded[count * 2] = 0;
            lm_json *block = lm_success();
            lm_set_string(block, "output_hex", encoded);
            if (root_reply(socket, block, -1))
                failed = 1;
        } else if (!count)
            eof = 1;
        else if (errno != EINTR && errno != EAGAIN)
            failed = 1;
        if (!exited) {
            siginfo_t info = {0};
            if (waitid(P_PID, child, &info, WEXITED | WNOHANG | WNOWAIT)) {
                if (errno != EINTR)
                    failed = 1;
            } else if (info.si_pid == child) {
                exited = 1;
                // Keep the zombie unreaped until group cleanup: its PID cannot be reused.
                kill(-child, SIGKILL);
            }
        }
        if (exited && count <= 0)
            break;
        if (count <= 0) {
            struct pollfd p = {output[0], POLLIN, 0};
            poll(&p, 1, eof ? 0 : 50);
            if (eof && !exited)
                usleep(50000);
        }
    }
    // A one-shot helper may not leave background tasks behind, even on success.
    kill(-child, SIGKILL);
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    close(output[0]);
    int result = failed ? -1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    lm_json *reply = failed
                         ? lm_failure("Root execution revoked, timed out or output limit exceeded")
                         : lm_success();
    lm_set_string(reply, "state", "finished");
    lm_set_integer(reply, "status", result);
    root_reply(socket, reply, -1);
    return result;
}
