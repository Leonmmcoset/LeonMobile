#include "init.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
static volatile sig_atomic_t action;
static void signal_handler(int sig) {
    if (sig != SIGCHLD)
        action = sig;
}
static void boot_child(const char *path) {
    execl(path, path, "--boot-console", "--log-socket", "/run/leon-log/capture.sock", (char *)NULL);
    leon_boot_error(path);
    _exit(127);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (getpid() != 1) {
        fputs("leon-init must run as PID 1\n", stderr);
        return 1;
    }
    leon_boot_console_enable();
    leon_boot_prepare();
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
        leon_boot_error("leond fork");
    while (!action) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid == manager) {
            leon_boot_log("LeonMobile: service manager exited; restarting in 2 seconds\n");
            sleep(2);
            manager = fork();
            if (!manager)
                boot_child("/sbin/leond");
        } else if (pid > 0) {
            // Reap and terminate an orphaned service's remaining process group.
            kill(-pid, SIGKILL);
        } else if (pid < 0 && errno != EINTR)
            pause();
    }
    leon_shutdown(manager, action);
}
