/* Disposable QEMU checks, including a genuinely full, isolated tmpfs. */
#define _GNU_SOURCE
#include "system_probe.h"
#include "log.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(test) do { if (!(test)) { fprintf(report, "FAIL logging:%d %s errno=%d reply=%.1000s\n", \
    __LINE__, #test, errno, probe_reply()); return 1; } } while (0)
static int service(const char *command) {
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    strcpy(a.sun_path, "/run/leon/services.sock");
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    char reply[128];
    int result = fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof(a)) ||
                 send(fd, command, strlen(command), MSG_NOSIGNAL) != (ssize_t)strlen(command);
    if (!result) {
        ssize_t n = read(fd, reply, sizeof(reply));
        result = n < 2 || memcmp(reply, "OK", 2);
    }
    if (fd >= 0) close(fd);
    return result;
}
static int wait_log(const char *request, const char *marker) {
    for (int i = 0; i < 100; ++i) {
        if (!probe_rpc("log", request, 0) && probe_has(marker))
            return 0;
        usleep(100000);
    }
    return -1;
}
static int full_storage(FILE *report) {
    CHECK(!mkdir("/run/log-full", 0700));
    CHECK(!mount("tmpfs", "/run/log-full", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC,
                 "size=64k,mode=0700"));
    int filler = open("/run/log-full/fill", O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    CHECK(filler >= 0);
    char block[4096] = {0};
    while (write(filler, block, sizeof(block)) > 0) {}
    CHECK(errno == ENOSPC);
    close(filler);
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (!pid) {
        execl("/usr/sbin/logd", "logd", "--test", "--runtime", "/run/log-full-runtime",
              "--state", "/run/log-full/journal", "--kmsg", "", (char *)NULL);
        _exit(127);
    }
    char *response = malloc(LEON_LOG_RESPONSE);
    CHECK(response);
    int ready = 0;
    for (int i = 0; i < 100; ++i) {
        if (!leon_log_exchange("/run/log-full-runtime/control.sock", "{\"op\":\"status\"}\n",
                               response, LEON_LOG_RESPONSE, 1000)) { ready = 1; break; }
        usleep(100000);
    }
    int failed = !ready;
    if (!failed) {
        failed = leon_log_write("/run/log-full-runtime/write.sock", 6, "full", "memory-on-full", 14);
        for (int i = 0; !failed && i < 100; ++i) {
            if (!leon_log_exchange("/run/log-full-runtime/control.sock", "{\"op\":\"status\"}\n",
                                   response, LEON_LOG_RESPONSE, 1000) &&
                strstr(response, "\"writer_error\":28")) { ready = 2; break; }
            usleep(100000);
        }
        failed |= ready != 2;
    }
    if (!failed) {
        failed = leon_log_exchange("/run/log-full-runtime/control.sock", "{\"op\":\"query\"}\n",
                                   response, LEON_LOG_RESPONSE, 1000) ||
                 !strstr(response, "memory-on-full");
    }
    unlink("/run/log-full/fill");
    ready = 0;
    for (int i = 0; !failed && i < 100; ++i) {
        if (!leon_log_exchange("/run/log-full-runtime/control.sock", "{\"op\":\"status\"}\n",
                               response, LEON_LOG_RESPONSE, 1000) &&
            strstr(response, "\"storage_healthy\":true")) { ready = 1; break; }
        usleep(100000);
    }
    failed |= !ready;
    free(response);
    kill(pid, SIGTERM);
    int status;
    for (int i = 0; i < 50; ++i) {
        if (waitpid(pid, &status, WNOHANG) == pid) { pid = 0; break; }
        usleep(100000);
    }
    if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }
    CHECK(!umount("/run/log-full"));
    CHECK(!failed);
    fputs("PASS logging: real ENOSPC preserves ingestion/query and recovers after freeing space\n", report);
    return 0;
}
int probe_logging(FILE *report) {
    CHECK(!wait_log("{\"op\":\"status\"}", "\"storage_healthy\":true"));
    CHECK(probe_has("\"kernel_available\":true"));
    int kernel = open("/dev/kmsg", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
    CHECK(kernel >= 0);
    const char message[] = "<6>LEON_LOG_KERNEL_PROBE\n";
    CHECK(write(kernel, message, sizeof(message) - 1) == sizeof(message) - 1);
    close(kernel);
    CHECK(!wait_log("{\"op\":\"query\",\"source\":\"kmsg\",\"tail\":true,\"limit\":128}",
                    "LEON_LOG_KERNEL_PROBE"));
    int saw_init = 0, saw_kernel = 0;
    long long after = 0;
    for (int i = 0; i < 32; ++i) {
        char request[256];
        snprintf(request, sizeof(request), "{\"op\":\"query\",\"source\":\"kmsg\",\"limit\":128,\"after\":%lld}", after);
        CHECK(!probe_rpc("log", request, 0));
        saw_init |= probe_has("leonboot: LeonMobile");
        saw_kernel |= probe_has("Linux version 7.3.1");
        after = probe_integer("next");
        if (!probe_has("\"more\":true")) break;
    }
    CHECK(saw_init && saw_kernel);
    puts("LEON_LOG_SERVICE_STDOUT");
    fflush(stdout);
    CHECK(!wait_log("{\"op\":\"query\",\"source\":\"system-test\"}", "LEON_LOG_SERVICE_STDOUT"));
    CHECK(probe_has("\"scope\":\"system\""));
    CHECK(!probe_rpc("log", "{\"op\":\"query\",\"source\":\"kmsg\"}", 1000));
    CHECK(probe_has("\"records\":[]") && probe_has("\"access\":\"own\""));
    CHECK(!probe_rpc("log", "{\"op\":\"status\"}", 11000));
    CHECK(probe_has("\"access\":\"own\""));
    CHECK(!service("stop logging\n"));
    usleep(700000);
    puts("LEON_LOG_OFFLINE_STDOUT");
    fflush(stdout);
    usleep(300000);
    CHECK(!probe_rpc("apps", "{\"op\":\"lifecycle\"}", 0) && probe_has("\"ok\":true"));
    CHECK(!service("start logging\n"));
    CHECK(!wait_log("{\"op\":\"query\",\"source\":\"system-test\"}", "LEON_LOG_OFFLINE_STDOUT"));
    CHECK(!wait_log("{\"op\":\"query\",\"source\":\"system-test\"}", "LEON_LOG_SERVICE_STDOUT"));
    fputs("PASS logging: real kernel/init/service capture, peer isolation, offline buffering and daemon restart history\n", report);
    CHECK(!full_storage(report));
    return 0;
}
