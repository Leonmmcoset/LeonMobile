#define _GNU_SOURCE
#include "system_probe.h"
#include "wire.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* Test-only executable injected into a disposable disk, never a product image. */
static FILE *report;
#define OK(request) (!probe_rpc("auth", request, 0) && probe_has("\"ok\":true"))
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(report, "FAIL line=%d %s response=%s\n", __LINE__, #condition, probe_reply()); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int unlock(const char *secret) {
    char request[512], token[65];
    snprintf(request, sizeof(request), "{\"op\":\"authenticate\",\"secret\":\"%s\"}", secret);
    if (!OK(request))
        return -1;
    const char *start = strstr(probe_reply(), "\"token\":\"");
    if (!start || strlen(start + 9) < 65)
        return -1;
    memcpy(token, start + 9, 64);
    token[64] = 0;
    snprintf(request, sizeof(request),
             "{\"op\":\"consume\",\"purpose\":\"unlock\",\"token\":\"%s\"}", token);
    if (!OK(request) || !probe_has("\"locked\":false"))
        return -1;
    return probe_rpc("auth", request, 0) || !probe_has("\"ok\":false") ? -1 : 0;
}
static int suite(void) {
    CHECK(OK("{\"op\":\"status\"}") && probe_has("\"locked\":true"));
    for (uid_t uid = 1000; uid <= 11000; uid += 10000)
        CHECK(probe_rpc("auth", "{\"op\":\"authenticate\"}", uid));
    if (!access("/data/leon/auth-test.done", F_OK)) {
        CHECK(OK("{\"op\":\"status\"}"));
        CHECK(probe_integer("generation") != 1);
        fprintf(report, "PASS device policy survived a real kernel reboot response=%s\n",
                probe_reply());
        return 0;
    }
    CHECK(!unlock(""));
    CHECK(OK("{\"op\":\"configure\",\"kind\":\"pin\",\"new_secret\":\"739182\"}"));
    CHECK(OK("{\"op\":\"lock\"}"));
    CHECK(!probe_rpc("auth", "{\"op\":\"authenticate\",\"secret\":\"incorrect\"}", 0) &&
          probe_has("\"ok\":false"));
    CHECK(OK("{\"op\":\"status\"}") && probe_integer("retry_after_ms") > 0);
    usleep(1100000);
    CHECK(!unlock("739182"));
    CHECK(OK("{\"op\":\"configure\",\"kind\":\"password\",\"secret\":\"739182\",\"new_secret\":"
             "\"Device passphrase 2026!\"}"));
    CHECK(OK("{\"op\":\"lock\"}"));
    CHECK(!unlock("Device passphrase 2026!"));
    CHECK(OK("{\"op\":\"configure\",\"kind\":\"none\",\"secret\":\"Device passphrase 2026!\"}"));
    CHECK(OK("{\"op\":\"lock\"}"));
    FILE *marker = fopen("/data/leon/auth-test.done", "w");
    CHECK(marker != NULL);
    fputs("native credential protocol verified\n", marker);
    fclose(marker);
    fprintf(
        report,
        "PASS native AArch64 PIN/password/disable, retry delay, one-use grants and forged peers\n");
    return 0;
}
static int privileged(const char *op) {
    if (geteuid())
        return 2;
    if (!strcmp(op, "status")) {
        if (probe_rpc("auth", "{\"op\":\"status\"}", 0))
            return 1;
        puts(probe_reply());
        return 0;
    }
    if (!strcmp(op, "bare-unlock")) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        struct sockaddr_un address = {.sun_family = AF_UNIX};
        strcpy(address.sun_path, "/run/leon/wm.sock");
        int result = fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) ||
                     send(fd, "unlock", 6, MSG_NOSIGNAL) != 6;
        if (fd >= 0)
            close(fd);
        return result;
    }
    if (!strcmp(op, "lock"))
        return probe_rpc("auth", "{\"op\":\"lock\"}", 0);
    const char *command =
        !strcmp(op, "stall")          ? "kill -STOP $(pidof authd)"
        : !strcmp(op, "resume")       ? "kill -CONT $(pidof authd)"
        : !strcmp(op, "crash")        ? "kill -KILL $(pidof authd)"
        : !strcmp(op, "shell-stall")  ? "kill -STOP $(pidof leon-shell)"
        : !strcmp(op, "shell-resume") ? "kill -CONT $(pidof leon-shell)"
        : !strcmp(op, "shell-crash")  ? "kill -KILL $(pidof leon-shell)"
        : !strcmp(op, "idle")         ? "lmsys power '{\"op\":\"policy\",\"idle_seconds\":2}'"
        : !strcmp(op, "idle-reset")   ? "lmsys power '{\"op\":\"policy\",\"idle_seconds\":300}'"
                                      : NULL;
    return command ? system(command) : 2;
}
static int control(const char *op) {
    for (const char *p = op; *p; ++p)
        if ((*p < 'a' || *p > 'z') && *p != '-')
            return 2;
    int fd = root_connect(LEON_ROOT_SOCKET), master = -1;
    if (fd < 0)
        return 1;
    char request[512], reply[LEON_ROOT_MESSAGE];
    snprintf(request, sizeof(request),
             "{\"op\":\"exec\",\"password\":\"LeonNext!2026\",\"command\":\"/usr/bin/"
             "leon-test-auth --privileged %s\"}\n",
             op);
    if (root_send(fd, request, -1) || root_receive(fd, reply, sizeof(reply), &master) ||
        master < 0) {
        fprintf(stderr, "test root control unavailable: %s\n", reply);
        close(fd);
        return 1;
    }
    char buffer[4096];
    for (;;) {
        struct pollfd stream = {master, POLLIN, 0};
        if (poll(&stream, 1, 15000) <= 0)
            break;
        ssize_t n = read(master, buffer, sizeof(buffer));
        if (n <= 0)
            break;
        if (write(STDOUT_FILENO, buffer, (size_t)n) != n)
            break;
    }
    int result = root_receive(fd, reply, sizeof(reply), NULL);
    close(master);
    close(fd);
    return result || !strstr(reply, "\"status\":0") ? 1 : 0;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--control"))
        return control(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--privileged"))
        return privileged(argv[2]);
    report = fopen("/run/leon/auth-tests.txt", "w");
    if (!report)
        return 2;
    setbuf(report, NULL);
    int result = suite();
    fprintf(report, "AUTH_TESTS_%s\n", result ? "FAILED" : "PASSED");
    fclose(report);
    return result;
}
