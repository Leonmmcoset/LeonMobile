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
int probe_logging(FILE *);
int probe_isolation(FILE *);
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
    pid_t background = (pid_t)probe_integer("pid");
    CHECK(background > 1);
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.leon.calculator\"}"));
    pid_t foreground = (pid_t)probe_integer("pid");
    CHECK(foreground > 1);
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
    CHECK(!kill(foreground, 0));
    CHECK(kill(background, 0) == -1 && errno == ESRCH);
    CHECK(RPC("apps", "{\"op\":\"lifecycle\"}"));
    CHECK(probe_has("\"foreground\":true"));
    CHECK(RPC("apps", "{\"op\":\"stop\",\"id\":\"org.leon.calculator\"}"));
    CHECK(RPC("apps", "{\"op\":\"stop\",\"id\":\"org.leon.gallery\"}"));
    passed("memory pressure reclaims background app; foreground survives");
    return 0;
}
static int power(void) {
    CHECK(RPC("power", "{\"op\":\"policy\",\"idle_seconds\":1}"));
    CHECK(!probe_rpc("power", "{\"op\":\"locked\",\"value\":false}", 0) &&
          probe_has("\"ok\":false"));
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
    CHECK(probe_has("\"inhibited\":true"));
    usleep(1200000);
    CHECK(RPC("power", "{\"op\":\"status\"}"));
    CHECK(probe_has("\"inhibited\":false"));
    CHECK(RPC("power", "{\"op\":\"policy\",\"idle_seconds\":300}"));
    CHECK(RPC("power", "{\"op\":\"status\"}"));
    CHECK(!probe_rpc("power", "{\"op\":\"poweroff\"}", 11004) && probe_has("\"ok\":false"));
    passed("authoritative authd lock state, inhibitor expiry and peer authorization");
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
        if (probe_has("\"configuration_pending\":false") && probe_integer("dhcp_pid") > 1 &&
            probe_has("\"connected\":true") && probe_has("10.0.2.15") && probe_has("nameserver 10.0.2.3")) {
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
    CHECK(RPC("network", "{\"op\":\"configure\",\"values\":{\"method\":\"static\",\"address\":\"10.0.2.99\",\"prefix\":24,\"gateway\":\"10.0.2.2\",\"dns\":[\"10.0.2.3\"]}}"));
    ready = 0;
    for (int i = 0; i < 200; ++i) {
        CHECK(RPC("network", "{\"op\":\"status\"}"));
        if (probe_has("\"configuration_pending\":false") && probe_has("\"ipv4\":\"10.0.2.99\"") &&
            probe_has("\"connected\":true") && probe_integer("dhcp_pid") == 0) {
            ready = 1;
            break;
        }
        usleep(100000);
    }
    CHECK(ready);
    CHECK(!probe_rpc("network", "{\"op\":\"renew\"}", 1000) && probe_has("\"ok\":false"));
    CHECK(RPC("network", "{\"op\":\"configure\",\"values\":{\"method\":\"dhcp\",\"dns\":[]}}"));
    ready = 0;
    for (int i = 0; i < 200; ++i) {
        CHECK(RPC("network", "{\"op\":\"status\"}"));
        if (probe_has("\"configuration_pending\":false") && probe_has("\"ipv4\":\"10.0.2.15\"") &&
            probe_has("\"connected\":true") && probe_integer("dhcp_pid") > 1) { ready = 1; break; }
        usleep(100000);
    }
    CHECK(ready);
    struct stat st;
    CHECK(!stat("/run/resolv.conf", &st) && (st.st_mode & 0444) == 0444);
    passed("network DHCP renewal, static IPv4/DNS, restore and supervised client restart");
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
    ssize_t n = readlink("/run/leon-system/localtime", path, sizeof(path) - 1);
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
    pid_t app = (pid_t)probe_integer("pid");
    CHECK(app > 1);
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
    // A stopped Wayland client cannot checkpoint or handle SIGTERM. Its close
    // must still return promptly, leave the broker usable and reach SIGKILL.
    CHECK(!kill(app, SIGSTOP));
    struct timespec before, after;
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &before));
    CHECK(RPC("apps", "{\"op\":\"close\",\"id\":\"org.leon.gallery\"}"));
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &after));
    CHECK((after.tv_sec - before.tv_sec) * 1000 + (after.tv_nsec - before.tv_nsec) / 1000000 <
          1500);
    CHECK(probe_has("\"stopping\":true") && probe_has("\"stop_phase\":\"shutdown\""));
    CHECK(!probe_rpc("apps", "{\"op\":\"foreground\",\"id\":\"org.leon.gallery\"}", 0) &&
          probe_has("\"ok\":false"));
    CHECK(!probe_rpc("apps", "{\"op\":\"close\",\"id\":\"org.leon.gallery\"}", 11004) &&
          probe_has("\"ok\":false"));
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.leon.calculator\"}"));
    pid_t foreground = (pid_t)probe_integer("pid");
    CHECK(foreground > 1);
    int saw_term = 0, reaped = 0;
    for (int i = 0; i < 60; ++i) {
        CHECK(RPC("apps", "{\"op\":\"close\",\"id\":\"org.leon.gallery\"}"));
        if (probe_has("\"stop_phase\":\"term\""))
            saw_term = 1;
        if (kill(app, 0) == -1 && errno == ESRCH) {
            reaped = 1;
            break;
        }
        usleep(100000);
    }
    CHECK(saw_term && reaped && !kill(foreground, 0));
    CHECK(RPC("apps", "{\"op\":\"lifecycle\"}"));
    CHECK(probe_has("\"status\":137") && probe_has("\"foreground\":true"));
    CHECK(RPC("apps", "{\"op\":\"stop\",\"id\":\"org.leon.calculator\"}"));
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
    passed("async close remains responsive, preserves foreground, escalates and kills detached "
           "cgroup members");
    return 0;
}
static int app_management(void) {
    CHECK(!probe_rpc("apps", "{\"op\":\"app_list\"}", 11000) && probe_has("\"ok\":true") &&
          probe_has("\"source\":\"system-image\""));
    CHECK(!probe_rpc("apps", "{\"op\":\"app_list\"}", 11002) && probe_has("\"ok\":false"));
    CHECK(!probe_rpc("apps", "{\"op\":\"app_clear\",\"id\":\"org.leon.settings\"}", 11000) &&
          probe_has("\"ok\":false"));
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.leon.gallery\"}"));
    pid_t app = (pid_t)probe_integer("pid");
    CHECK(app > 1);
    CHECK(!probe_rpc("apps", "{\"op\":\"app_uninstall\",\"id\":\"org.leon.gallery\"}", 11000) &&
          probe_has("\"ok\":false"));
    CHECK(!kill(app, 0));
    const char *home = "/data/leon/data/org.leon.gallery";
    const char *secret = "/data/leon/shared/management-keep.txt";
    int fd = open(secret, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0 && write(fd, "keep", 4) == 4);
    close(fd);
    CHECK(!symlink(secret, "/data/leon/data/org.leon.gallery/external"));
    int gate[2];
    CHECK(!pipe(gate));
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(gate[1]);
        char go;
        if (read(gate[0], &go, 1) != 1 || setgid(11002) || setuid(11002) || setsid() < 0)
            _exit(2);
        close(gate[0]);
        for (;;) {
            int writer = open("/data/leon/data/org.leon.gallery/late-writer", O_WRONLY | O_CREAT, 0600);
            if (writer >= 0) {
                (void)write(writer, "x", 1);
                close(writer);
            }
            usleep(10000);
        }
    }
    close(gate[0]);
    CHECK(!leon_cgroup(child, 11002));
    CHECK(write(gate[1], "1", 1) == 1);
    close(gate[1]);
    usleep(150000);
    CHECK(!kill(app, SIGSTOP));
    CHECK(!probe_rpc("apps", "{\"op\":\"app_info\",\"id\":\"org.leon.gallery\"}", 11000) &&
          probe_has("\"ok\":true") && probe_has("\"available\":true"));
    CHECK(!probe_rpc("apps", "{\"op\":\"app_clear\",\"id\":\"org.leon.gallery\"}", 11000) &&
          probe_has("\"ok\":true"));
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    CHECK(access(home, F_OK) && errno == ENOENT);
    CHECK(access("/run/user/11002", F_OK) && errno == ENOENT);
    CHECK(!access(secret, F_OK));
    CHECK(!probe_rpc("apps", "{\"op\":\"app_info\",\"id\":\"org.leon.gallery\"}", 11000) &&
          probe_has("\"running\":false") && probe_has("\"bytes\":0"));
    CHECK(!probe_rpc("portal", "{\"op\":\"reset_app\",\"app\":\"org.leon.gallery\"}", 11000) &&
          probe_has("\"ok\":false"));
    CHECK(RPC("apps", "{\"op\":\"launch\",\"id\":\"org.leon.gallery\"}"));
    CHECK(RPC("apps", "{\"op\":\"app_stop\",\"id\":\"org.leon.gallery\"}"));
    CHECK(!unlink(secret)); // Do not change the later file-picker fixture.
    passed("Settings management: real UID authorization, protected apps, bounded storage, forced stop, detached writer cleanup and relaunch");
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
    if (geteuid() || probe_logging(report) || sample_memory() || power() || storage() || network() || portal() || timed() ||
        cleanup() || app_management() || crash() || probe_isolation(report)) {
        fputs("SYSTEM_TESTS_FAILED\n", report);
        fclose(report);
        return 1;
    }
    fputs("SYSTEM_TESTS_PASSED\n", report);
    fclose(report);
    return 0;
}
