#define _GNU_SOURCE
#include "../../src/services/appd/sandbox.h"
#include "system_probe.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static FILE *report;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(report, "FAIL %s:%d %s response=%s errno=%d\n", __FILE__, __LINE__,            \
                    #condition, probe_reply(), errno);                                             \
            fflush(report);                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define RPC(service, request) (probe_rpc(service, request, 0) == 0 && probe_has("\"ok\":true"))
static void passed(const char *stage) {
    fprintf(report, "PASS %s\n", stage);
    fflush(report);
}
static int sample_memory(void) {
    CHECK(RPC("memory", "{\"op\":\"status\"}"));
    CHECK(probe_has("\"psi_available\":true"));
    long long available = probe_integer("available_kb");
    CHECK(available > 0);
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.leon.gallery\"}"));
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.leon.calculator\"}"));
    CHECK(RPC("memory", "{\"op\":\"policy\",\"min_available_mb\":1024}"));
    size_t size = available > 800 * 1024 ? (size_t)(available - 800 * 1024) * 1024 : 0;
    CHECK(size < 1600ULL * 1024 * 1024);
    volatile char *memory = size ? malloc(size) : NULL;
    CHECK(!size || memory);
    for (size_t i = 0; i < size; i += 4096)
        memory[i] = 1;
    int reclaimed = 0;
    for (int i = 0; i < 80; ++i) {
        CHECK(RPC("memory", "{\"op\":\"status\"}"));
        if (probe_integer("reclaimed") > 0) {
            reclaimed = 1;
            break;
        }
        usleep(100000);
    }
    free((void *)memory);
    CHECK(RPC("memory", "{\"op\":\"policy\",\"min_available_mb\":96}"));
    CHECK(reclaimed);
    CHECK(RPC("apps", "{\"op\":\"list\"}"));
    CHECK(probe_has("org.leon.calculator"));
    CHECK(RPC("apps", "{\"op\":\"lifecycle\"}"));
    CHECK(probe_has("\"foreground\":true"));
    CHECK(RPC("apps", "{\"op\":\"stop\",\"id\":\"org.leon.calculator\"}"));
    CHECK(RPC("apps", "{\"op\":\"stop\",\"id\":\"org.leon.gallery\"}"));
    passed("memory pressure reclaims background app; foreground survives");
    return 0;
}
static int power(void) {
    CHECK(RPC("power", "{\"op\":\"policy\",\"idle_seconds\":1}"));
    CHECK(RPC("power", "{\"op\":\"locked\",\"value\":false}"));
    CHECK(
        !probe_rpc("power", "{\"op\":\"inhibit\",\"seconds\":2,\"reason\":\"QEMU test\"}", 11002) &&
        probe_has("\"ok\":true"));
    long long token = probe_integer("token");
    CHECK(token > 0);
    char request[128];
    snprintf(request, sizeof(request), "{\"op\":\"release\",\"token\":%lld}", token);
    CHECK(!probe_rpc("power", request, 11004) && probe_has("\"ok\":false"));
    usleep(1200000);
    CHECK(RPC("power", "{\"op\":\"status\"}"));
    CHECK(probe_has("\"lock_requested\":false"));
    usleep(1200000);
    CHECK(RPC("power", "{\"op\":\"status\"}"));
    CHECK(probe_has("\"lock_requested\":true"));
    CHECK(RPC("power", "{\"op\":\"policy\",\"idle_seconds\":300}"));
    CHECK(RPC("power", "{\"op\":\"locked\",\"value\":true}"));
    CHECK(!probe_rpc("power", "{\"op\":\"poweroff\"}", 11004) && probe_has("\"ok\":false"));
    passed("power idle, inhibitor expiry and peer authorization");
    return 0;
}
static int storage(void) {
    CHECK(RPC("storage", "{\"op\":\"status\"}"));
    CHECK(probe_has("sda"));
    CHECK(probe_has("\"removable\":true"));
    CHECK(!probe_rpc("storage", "{\"op\":\"mount\",\"device\":\"../vda1\"}", 0) &&
          probe_has("\"ok\":false"));
    CHECK(!probe_rpc("storage", "{\"op\":\"mount\",\"device\":\"vda1\"}", 0) &&
          probe_has("\"ok\":false"));
    CHECK(RPC("storage", "{\"op\":\"mount\",\"device\":\"sda\"}"));
    CHECK(access("/media/sda/lost+found", F_OK) == 0);
    int fd = open("/media/sda/no-write", O_CREAT | O_WRONLY, 0600);
    CHECK(fd < 0 && errno == EROFS);
    int directory = open("/media/sda", O_RDONLY | O_DIRECTORY);
    CHECK(directory >= 0);
    CHECK(fchdir(directory) == 0);
    CHECK(!probe_rpc("storage", "{\"op\":\"unmount\",\"device\":\"sda\"}", 0) &&
          probe_has("\"ok\":false"));
    CHECK(!chdir("/"));
    close(directory);
    CHECK(RPC("storage", "{\"op\":\"unmount\",\"device\":\"sda\"}"));
    passed("removable ext4 mount, read-only enforcement, busy unmount and recovery");
    return 0;
}
static int network(void) {
    CHECK(RPC("network", "{\"op\":\"disable\"}"));
    CHECK(probe_has("\"enabled\":false"));
    CHECK(RPC("network", "{\"op\":\"enable\"}"));
    int ready = 0;
    for (int i = 0; i < 200; ++i) {
        CHECK(RPC("network", "{\"op\":\"status\"}"));
        if (probe_has("10.0.2.15") && probe_has("nameserver 10.0.2.3")) {
            ready = 1;
            break;
        }
        usleep(100000);
    }
    CHECK(ready);
    pid_t old = (pid_t)probe_integer("dhcp_pid");
    CHECK(old > 1);
    CHECK(!kill(old, SIGKILL));
    ready = 0;
    for (int i = 0; i < 200; ++i) {
        CHECK(RPC("network", "{\"op\":\"status\"}"));
        if (probe_integer("dhcp_pid") > 0 && probe_integer("dhcp_pid") != old &&
            probe_has("10.0.2.15")) {
            ready = 1;
            break;
        }
        usleep(100000);
    }
    CHECK(ready);
    CHECK(RPC("network", "{\"op\":\"renew\"}"));
    struct stat st;
    CHECK(!stat("/run/resolv.conf", &st) && (st.st_mode & 0444) == 0444);
    passed("network disable/enable, real DHCP/DNS and supervised client restart");
    return 0;
}
static int portal(void) {
    CHECK(!probe_rpc("portal", "{\"op\":\"request\",\"permission\":\"files.import\"}", 11004) &&
          probe_has("\"ok\":true"));
    long long token = probe_integer("token");
    CHECK(token > 0);
    char request[256];
    snprintf(request, sizeof(request), "{\"op\":\"decide\",\"token\":%lld,\"allow\":true}", token);
    CHECK(!probe_rpc("portal", request, 11004) && probe_has("\"ok\":false"));
    CHECK(RPC("portal", request));
    int fd = open("/data/leon/shared/target-note.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    CHECK(write(fd, "LeonMobile 中文\n", 18) > 0);
    close(fd);
    CHECK(!probe_rpc("portal", "{\"op\":\"choose\"}", 11004) && probe_has("\"ok\":true"));
    token = probe_integer("token");
    CHECK(token > 0);
    snprintf(request, sizeof(request),
             "{\"op\":\"pick\",\"token\":%lld,\"file\":\"../system/grants.json\"}", token);
    CHECK(!probe_rpc("portal", request, 0) && probe_has("\"ok\":false"));
    snprintf(request, sizeof(request),
             "{\"op\":\"pick\",\"token\":%lld,\"file\":\"target-note.txt\"}", token);
    CHECK(RPC("portal", request));
    snprintf(request, sizeof(request), "{\"op\":\"selection\",\"token\":%lld}", token);
    CHECK(!probe_rpc("portal", request, 11002) && probe_has("\"ok\":false"));
    CHECK(!probe_rpc("portal", request, 11004) && probe_has("\"done\":true") &&
          probe_has("/imports/document-"));
    CHECK(RPC("portal",
              "{\"op\":\"revoke\",\"app\":\"org.leon.editor\",\"permission\":\"files.import\"}"));
    passed("portal confirmation, selected file import, traversal denial and revocation");
    return 0;
}
static void stamp(unsigned char *p, time_t seconds, long nano) {
    uint32_t word = htonl((uint32_t)((int64_t)seconds + 2208988800LL));
    memcpy(p, &word, 4);
    word = htonl((uint32_t)(((uint64_t)nano << 32) / 1000000000ULL));
    memcpy(p + 4, &word, 4);
}
static int timed(void) {
    CHECK(RPC("time", "{\"op\":\"configure\",\"automatic\":false,\"timezone\":\"Asia/Shanghai\"}"));
    char path[256];
    ssize_t n = readlink("/run/localtime", path, sizeof(path) - 1);
    CHECK(n > 0);
    path[n] = 0;
    CHECK(strstr(path, "Asia/Shanghai"));
    CHECK(RPC("time", "{\"op\":\"write_rtc\"}"));
    CHECK(probe_has("\"rtc_available\":true"));
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    CHECK(fd >= 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(123), .sin_addr = {htonl(INADDR_LOOPBACK)}};
    CHECK(!bind(fd, (struct sockaddr *)&addr, sizeof(addr)));
    pid_t server = fork();
    if (!server) {
        struct sockaddr_in peer;
        socklen_t length = sizeof(peer);
        unsigned char req[48], reply[48] = {0};
        alarm(8);
        if (recvfrom(fd, req, sizeof(req), 0, (struct sockaddr *)&peer, &length) != 48)
            _exit(1);
        reply[0] = 0x24;
        reply[1] = 2;
        memcpy(reply + 24, req + 40, 8);
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        stamp(reply + 32, now.tv_sec, now.tv_nsec);
        stamp(reply + 40, now.tv_sec, now.tv_nsec);
        _exit(sendto(fd, reply, 48, 0, (struct sockaddr *)&peer, length) == 48 ? 0 : 2);
    }
    close(fd);
    CHECK(server > 0);
    CHECK(RPC("time", "{\"op\":\"configure\",\"automatic\":true,\"server\":\"127.0.0.1\"}"));
    int synced = 0;
    for (int i = 0; i < 60; ++i) {
        CHECK(RPC("time", "{\"op\":\"status\"}"));
        if (probe_has("\"synchronized\":true")) {
            synced = 1;
            break;
        }
        usleep(100000);
    }
    int status;
    waitpid(server, &status, 0);
    CHECK(WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(synced);
    CHECK(RPC("time", "{\"op\":\"configure\",\"timezone\":\"Etc/"
                      "UTC\",\"server\":\"162.159.200.1\",\"automatic\":true}"));
    passed("timezone switch, RTC write/read and correlated SNTP exchange");
    return 0;
}
static int cleanup(void) {
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.leon.gallery\"}"));
    int gate[2];
    CHECK(!pipe(gate));
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(gate[1]);
        char go;
        if (read(gate[0], &go, 1) != 1)
            _exit(1);
        close(gate[0]);
        if (setgid(11002) || setuid(11002))
            _exit(2);
        setsid();
        for (;;)
            pause();
    }
    close(gate[0]);
    CHECK(!leon_cgroup(child, 11002));
    CHECK(write(gate[1], "1", 1) == 1);
    close(gate[1]);
    usleep(100000);
    CHECK(RPC("apps", "{\"op\":\"stop\",\"id\":\"org.leon.gallery\"}"));
    int status = 0;
    pid_t result = 0;
    for (int i = 0; i < 50; ++i) {
        result = waitpid(child, &status, WNOHANG);
        if (result == child)
            break;
        usleep(100000);
    }
    if (result != child) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    CHECK(result == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    CHECK(RPC("crash", "{\"op\":\"list\"}"));
    CHECK(probe_has("\"exits\":[{"));
    passed("app stop kills detached cgroup members and persists observed exits");
    return 0;
}
static int crash(void) {
    CHECK(!probe_sandbox());
    int captured = 0;
    for (int i = 0; i < 60; ++i) {
        CHECK(RPC("crash", "{\"op\":\"list\"}"));
        if (probe_has("\"signal\":11") && probe_has("org.leon.gallery") &&
            probe_integer("core_bytes") > 0) {
            captured = 1;
            break;
        }
        usleep(100000);
    }
    CHECK(captured);
    CHECK(!probe_rpc("crash", "{\"op\":\"list\"}", 11004) && probe_has("\"reports\":[]"));
    passed("Landlock/seccomp allow-deny, cgroup process/memory enforcement and real core capture");
    return 0;
}
int main(void) {
    umask(022);
    report = fopen("/run/leon/system-tests.txt", "w");
    if (!report)
        return 2;
    setbuf(report, NULL);
    if (geteuid() || sample_memory() || power() || storage() || network() || portal() || timed() ||
        cleanup() || crash()) {
        fputs("SYSTEM_TESTS_FAILED\n", report);
        fclose(report);
        return 1;
    }
    fputs("SYSTEM_TESTS_PASSED\n", report);
    fclose(report);
    return 0;
}
