#define _GNU_SOURCE
#include "system_probe.h"
#include <stdio.h>
#include <string.h>
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
        fprintf(report, "PASS device policy survived a real kernel reboot\n");
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
int main(void) {
    report = fopen("/run/leon/auth-tests.txt", "w");
    if (!report)
        return 2;
    setbuf(report, NULL);
    int result = suite();
    fprintf(report, "AUTH_TESTS_%s\n", result ? "FAILED" : "PASSED");
    fclose(report);
    return result;
}
