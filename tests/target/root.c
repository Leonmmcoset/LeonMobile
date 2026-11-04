#define _GNU_SOURCE
#include "system_probe.h"
#include "wire.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Only injected into a disposable QEMU disk. Never installed in release images. */
#define FIRST "LeonRoot!2026"
#define SECOND "LeonNext!2026"
#define OK(request) (!probe_rpc("root", request, 0) && probe_has("\"ok\":true"))
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(report, "FAIL line=%d %s response=%s errno=%d\n", __LINE__, #condition,        \
                    probe_reply(), errno);                                                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static FILE *report;
static void passed(const char *message) { fprintf(report, "PASS %s\n", message); }
static int session(const char *password, const char *command, int *connection, int *master,
                   pid_t *pid) {
    *connection = root_connect(LEON_ROOT_SOCKET);
    *master = -1;
    char request[2048], response[LEON_ROOT_MESSAGE];
    snprintf(
        request, sizeof(request),
        "{\"op\":\"exec\",\"password\":\"%s\",\"command\":\"%s\",\"rows\":31,\"columns\":92}\n",
        password, command);
    if (*connection < 0 || root_send(*connection, request, -1) ||
        root_receive(*connection, response, sizeof(response), master) || *master < 0)
        return -1;
    char *number = strstr(response, "\"pid\":");
    *pid = number ? (pid_t)strtol(number + 6, NULL, 10) : -1;
    return *pid > 1 ? 0 : -1;
}
static int terminal(int master, const char *marker) {
    char output[4096] = {0};
    size_t used = 0;
    for (int i = 0; i < 100; ++i) {
        struct pollfd fd = {master, POLLIN, 0};
        if (poll(&fd, 1, 50) <= 0)
            continue;
        ssize_t n = read(master, output + used, sizeof(output) - 1 - used);
        if (n <= 0)
            break;
        used += (size_t)n;
        output[used] = 0;
        if (strstr(output, marker))
            return 0;
        if (used == sizeof(output) - 1)
            break;
    }
    fprintf(report, "PTY output: %s\n", output);
    return -1;
}
static int gone(pid_t pid) {
    for (int i = 0; i < 100; ++i) {
        if (kill(pid, 0) && errno == ESRCH)
            return 0;
        usleep(50000);
    }
    return -1;
}
static int isolated(pid_t pid) {
    char path[64], text[256];
    snprintf(path, sizeof(path), "/proc/%d/cgroup", pid);
    FILE *file = fopen(path, "r");
    if (!file)
        return -1;
    int ok = fgets(text, sizeof(text), file) && strstr(text, "0::/leon/root/s");
    fclose(file);
    return ok ? 0 : -1;
}
static int read_pid(void) {
    for (int i = 0; i < 100; ++i) {
        FILE *file = fopen("/run/leon-root/detached.pid", "r");
        if (file) {
            int pid = -1;
            int count = fscanf(file, "%d", &pid);
            fclose(file);
            if (count == 1 && pid > 1)
                return pid;
        }
        usleep(50000);
    }
    return -1;
}
static int first_boot(void) {
    CHECK(OK("{\"op\":\"status\"}"));
    CHECK(probe_has("\"enabled\":false") && probe_has("\"password_set\":false") &&
          probe_has("\"sudo_enabled\":false"));
    CHECK(!probe_rpc("root", "{\"op\":\"enable\",\"password\":\"" FIRST "\"}", 11000) &&
          probe_has("\"ok\":false"));
    CHECK(!probe_rpc("root", "{\"op\":\"exec\",\"password\":\"" FIRST "\",\"uid\":0}", 11001) &&
          probe_has("\"ok\":false"));
    CHECK(!probe_rpc("root", "{\"op\":\"exec\",\"password\":\"" FIRST "\"}", 1000) &&
          probe_has("\"ok\":false"));
    CHECK(OK("{\"op\":\"enable\",\"password\":\"" FIRST "\"}"));
    CHECK(!probe_rpc("root", "{\"op\":\"disable\",\"password\":\"wrong-password\"}", 0) &&
          probe_has("\"ok\":false"));
    CHECK(OK("{\"op\":\"status\"}") && probe_integer("retry_after_ms") > 0);
    usleep(1100000);
    passed("default root lock, real peer/executable identity and password cooldown");

    CHECK(OK("{\"op\":\"sysctl\",\"area\":\"kernel\",\"path\":\"kernel/"
             "hostname\",\"value\":\"root-probe\",\"password\":\"" FIRST "\"}"));
    CHECK(OK("{\"op\":\"inspect\",\"area\":\"kernel\",\"path\":\"kernel/hostname\"}") &&
          probe_has("root-probe"));
    CHECK(OK("{\"op\":\"sysctl\",\"area\":\"kernel\",\"path\":\"kernel/"
             "hostname\",\"value\":\"leonmobile\",\"password\":\"" FIRST "\"}"));
    CHECK(
        !probe_rpc("root", "{\"op\":\"inspect\",\"area\":\"kernel\",\"path\":\"../cmdline\"}", 0) &&
        probe_has("\"ok\":false"));
    CHECK(OK("{\"op\":\"inspect\",\"area\":\"core\"}") && probe_has("7.3.1-leonmobile") &&
          probe_has("aarch64"));
    CHECK(OK("{\"op\":\"inspect\",\"area\":\"modules\",\"available\":true}"));
    CHECK(OK("{\"op\":\"module\",\"area\":\"modules\",\"action\":\"load\",\"name\":\"bridge\","
             "\"password\":\"" FIRST "\"}"));
    CHECK(OK("{\"op\":\"inspect\",\"area\":\"modules\"}") && probe_has("\"name\":\"bridge\""));
    CHECK(OK("{\"op\":\"module\",\"area\":\"modules\",\"action\":\"unload\",\"name\":\"bridge\","
             "\"password\":\"" FIRST "\"}"));
    CHECK(OK("{\"op\":\"inspect\",\"area\":\"modules\"}") && !probe_has("\"name\":\"bridge\""));
    passed("real runtime sysctl write/restore, core inventory and bridge module load/unload");

    const char *actions[] = {"stop", "start", "restart"};
    for (size_t i = 0; i < 3; ++i) {
        char request[512];
        snprintf(request, sizeof(request),
                 "{\"op\":\"service_control\",\"area\":\"services\",\"name\":\"root-test-helper\","
                 "\"action\":\"%s\",\"password\":\"" FIRST "\"}",
                 actions[i]);
        CHECK(OK(request));
    }
    CHECK(OK(
        "{\"op\":\"service_config\",\"area\":\"services\",\"name\":\"root-test-helper\","
        "\"autostart\":false,\"restart\":\"on-failure\",\"restart_limit\":7,\"password\":\"" FIRST
        "\"}"));
    CHECK(OK("{\"op\":\"inspect\",\"area\":\"services\",\"name\":\"root-test-helper\"}") &&
          probe_has("\"autostart\":false") && probe_integer("restart_limit") == 7);
    passed("authenticated service stop/start/restart and durable boot/restart policy");

    int connection, master;
    pid_t pid;
    CHECK(
        !session(FIRST, "id -u; stty size; printf ROOT_READY; exit 7", &connection, &master, &pid));
    CHECK(!terminal(master, "0\r\n31 92\r\nROOT_READY"));
    char response[LEON_ROOT_MESSAGE];
    CHECK(!root_receive(connection, response, sizeof(response), NULL) &&
          strstr(response, "\"status\":7"));
    close(master);
    close(connection);
    CHECK(!session(FIRST,
                   "setsid sh -c 'echo $$ > /run/leon-root/detached.pid; exec sleep 600' "
                   "</dev/null >/dev/null 2>&1 & printf ROOT_READY; wait",
                   &connection, &master, &pid));
    CHECK(!terminal(master, "ROOT_READY"));
    int detached = read_pid();
    CHECK(detached > 1 && !isolated(pid) && !isolated(detached));
    CHECK(OK("{\"op\":\"password\",\"password\":\"" FIRST "\",\"new_password\":\"" SECOND "\"}"));
    CHECK(!gone(pid) && !gone(detached));
    close(master);
    close(connection);
    unlink("/run/leon-root/detached.pid");
    CHECK(!session(SECOND, "printf ROOT_READY; exec sleep 600", &connection, &master, &pid));
    CHECK(!terminal(master, "ROOT_READY"));
    CHECK(OK("{\"op\":\"disable\",\"password\":\"" SECOND "\"}"));
    CHECK(!gone(pid));
    close(master);
    close(connection);
    CHECK(OK("{\"op\":\"enable\",\"password\":\"" SECOND "\"}"));
    CHECK(!session(SECOND, "printf ROOT_READY; exec sleep 600", &connection, &master, &pid));
    CHECK(!terminal(master, "ROOT_READY"));
    close(connection);
    CHECK(!gone(pid));
    close(master);
    passed("root PTY UID/size/exit status, dedicated cgroups, detached descendant revocation, "
           "disable and disconnect cleanup");
    int connections[6], masters[6];
    pid_t children[6];
    for (int i = 0; i < 6; ++i) {
        CHECK(!session(SECOND, "printf ROOT_READY; exec sleep 600", &connections[i], &masters[i],
                       &children[i]));
        CHECK(!terminal(masters[i], "ROOT_READY"));
    }
    CHECK(session(SECOND, "id -u", &connection, &master, &pid));
    close(connection);
    CHECK(OK("{\"op\":\"status\"}"));
    CHECK(OK("{\"op\":\"disable\",\"password\":\"" SECOND "\"}"));
    for (int i = 0; i < 6; ++i) {
        CHECK(!gone(children[i]));
        close(connections[i]);
        close(masters[i]);
    }
    CHECK(OK("{\"op\":\"enable\",\"password\":\"" SECOND "\"}"));
    passed("six-session capacity limit reserves control workers; all sessions remain revocable");
    return 0;
}
static int later_boot(int phase) {
    CHECK(OK("{\"op\":\"status\"}") && probe_has("\"password_set\":true"));
    CHECK(probe_has(phase == 2 ? "\"enabled\":true" : "\"enabled\":false"));
    CHECK(OK("{\"op\":\"inspect\",\"area\":\"services\",\"name\":\"root-test-helper\"}"));
    CHECK(probe_has("\"autostart\":false") && probe_has("stopped") &&
          probe_integer("restart_limit") == 7);
    if (phase == 3)
        CHECK(OK("{\"op\":\"enable\",\"password\":\"" SECOND "\"}"));
    CHECK(!probe_rpc("root", "{\"op\":\"disable\",\"password\":\"" FIRST "\"}", 0) &&
          probe_has("\"ok\":false"));
    usleep(1100000);
    int connection, master;
    pid_t pid;
    CHECK(!session(
        SECOND, "sudo id >/dev/null 2>&1; test $? -eq 126 && printf SUDO_DISABLED; id -u; exit 7",
        &connection, &master, &pid));
    CHECK(!terminal(master, "SUDO_DISABLED0\r\n"));
    close(master);
    close(connection);
    CHECK(OK("{\"op\":\"disable\",\"password\":\"" SECOND "\"}"));
    passed(phase == 2 ? "enabled policy, rotated password and service overrides survive actual "
                        "reboot; sudo rejects UID 0"
                      : "disabled policy and retained password survive second reboot; re-enable "
                        "still authenticates");
    return 0;
}
int main(void) {
    report = fopen("/run/leon/root-tests.txt", "w");
    if (!report)
        return 2;
    setbuf(report, NULL);
    int phase = 1;
    FILE *previous = fopen("/data/leon/root-probe-phase", "r");
    if (previous) {
        if (fscanf(previous, "%d", &phase) != 1)
            phase = 0;
        else
            ++phase;
        fclose(previous);
    }
    if (geteuid() || phase < 1 || phase > 3 || (phase == 1 ? first_boot() : later_boot(phase))) {
        fputs("ROOT_TESTS_FAILED\n", report);
        fclose(report);
        return 1;
    }
    int fd = open("/data/leon/root-probe-phase", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    char text[16];
    int length = snprintf(text, sizeof(text), "%d\n", phase);
    if (fd < 0 || write(fd, text, (size_t)length) != length || fsync(fd))
        return 1;
    close(fd);
    fprintf(report, "ROOT_TESTS_PASSED phase=%d\n", phase);
    fclose(report);
    return 0;
}
