#include "service.h"
#include "daemon.h"
#include "ntp.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct time_state {
    int fd, automatic, synced;
    long long next, deadline;
    unsigned char request[48];
    struct timespec sent;
    char zone[128], server[64], error[128];
};
static void store32(unsigned char *p, uint32_t n) {
    n = htonl(n);
    memcpy(p, &n, 4);
}
static int zone_valid(const char *s) {
    if (!*s || strlen(s) > 120 || s[0] == '/' || strstr(s, ".."))
        return 0;
    for (const char *p = s; *p; ++p)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              strchr("/_+-", *p)))
            return 0;
    return 1;
}
static int persist(struct lm_daemon *d) {
    struct time_state *s = d->data;
    lm_json *j = lm_object();
    lm_set_boolean(j, "automatic", s->automatic);
    lm_set_string(j, "timezone", s->zone);
    lm_set_string(j, "server", s->server);
    int result = lm_write_json(d->options.state, j);
    lm_free(j);
    return result;
}
static int apply_zone(struct lm_daemon *d, const char *zone) {
    if (!zone_valid(zone))
        return -1;
    if (d->options.test)
        return 0;
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "/usr/share/zoneinfo/%s", zone);
    int fd = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    char magic[4];
    int valid = read(fd, magic, 4) == 4 && !memcmp(magic, "TZif", 4);
    close(fd);
    if (!valid)
        return -1;
    unlink("/run/localtime.new");
    if (symlink(source, "/run/localtime.new") || rename("/run/localtime.new", "/run/localtime"))
        return -1;
    setenv("TZ", zone, 1);
    tzset();
    return 0;
}
static void tick(struct lm_daemon *d) {
    struct time_state *s = d->data;
    if (!s->automatic || d->options.test)
        return;
    if (s->fd >= 0) {
        unsigned char packet[512];
        ssize_t n = recv(s->fd, packet, sizeof(packet), MSG_DONTWAIT);
        if (n >= 48) {
            struct timespec now, target;
            clock_gettime(CLOCK_REALTIME, &now);
            if (leon_ntp_sample(packet, (size_t)n, s->request, s->sent, now, &target)) {
                snprintf(s->error, sizeof(s->error), "invalid or out-of-policy NTP sample");
            } else if (clock_settime(CLOCK_REALTIME, &target)) {
                snprintf(s->error, sizeof(s->error), "cannot set system clock");
            } else {
                s->synced = 1;
                s->error[0] = 0;
            }
            close(s->fd);
            s->fd = -1;
            s->next = lm_now_ms() + (s->synced ? 3600000 : 60000);
        } else if (lm_now_ms() >= s->deadline) {
            close(s->fd);
            s->fd = -1;
            s->next = lm_now_ms() + 60000;
            snprintf(s->error, sizeof(s->error), "NTP response timed out");
        }
        return;
    }
    if (lm_now_ms() < s->next)
        return;
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(123)};
    if (inet_pton(AF_INET, s->server, &peer.sin_addr) != 1)
        return;
    s->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (s->fd < 0) {
        s->next = lm_now_ms() + 60000;
        return;
    }
    memset(s->request, 0, sizeof(s->request));
    s->request[0] = 0x23;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    s->sent = now;
    store32(s->request + 40, (uint32_t)((int64_t)now.tv_sec + 2208988800LL));
    store32(s->request + 44, (uint32_t)(((uint64_t)now.tv_nsec << 32) / 1000000000ULL));
    if (connect(s->fd, (struct sockaddr *)&peer, sizeof(peer)) ||
        send(s->fd, s->request, 48, 0) != 48) {
        close(s->fd);
        s->fd = -1;
        s->next = lm_now_ms() + 60000;
        return;
    }
    s->deadline = lm_now_ms() + 3000;
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct time_state *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (strcmp(op, "status")) {
        if (uid != 0 && uid != 11000 && uid != 1000)
            return lm_failure("permission denied");
        if (!strcmp(op, "configure")) {
            struct time_state old = *s;
            if ((lm_get(r, "timezone") && !lm_is_string(lm_get(r, "timezone"))) ||
                (lm_get(r, "server") && !lm_is_string(lm_get(r, "server"))) ||
                (lm_get(r, "automatic") && lm_boolean(lm_get(r, "automatic"), -1) < 0))
                return lm_failure("invalid configuration types");
            const char *zone = lm_string(lm_get(r, "timezone"), old.zone),
                       *server = lm_string(lm_get(r, "server"), old.server);
            struct in_addr address;
            int automatic = lm_boolean(lm_get(r, "automatic"), s->automatic);
            if (!zone_valid(zone) || inet_pton(AF_INET, server, &address) != 1 ||
                apply_zone(d, zone))
                return lm_failure("invalid or unavailable timezone/server");
            snprintf(s->zone, sizeof(s->zone), "%s", zone);
            snprintf(s->server, sizeof(s->server), "%s", server);
            s->automatic = automatic;
            if (persist(d)) {
                *s = old;
                apply_zone(d, old.zone);
                return lm_failure("cannot persist time settings");
            }
            if (s->fd >= 0)
                close(s->fd);
            s->fd = -1;
            s->next = 0;
            s->synced = 0;
        } else if (!strcmp(op, "set_time")) {
            long long seconds = lm_integer(lm_get(r, "unix_seconds"), -1);
            if (seconds < 1577836800LL || seconds > 4102444800LL)
                return lm_failure("time outside 2020..2100");
            if (d->options.test)
                return lm_failure("clock mutations disabled for test roots");
            struct timespec value = {.tv_sec = seconds};
            if (clock_settime(CLOCK_REALTIME, &value))
                return lm_failure(strerror(errno));
            s->synced = 0;
        } else if (!strcmp(op, "write_rtc")) {
            if (d->options.test)
                return lm_failure("RTC writes disabled for test roots");
            int fd = open("/dev/rtc0", O_RDWR | O_CLOEXEC);
            time_t now = time(NULL);
            struct tm tm;
            gmtime_r(&now, &tm);
            struct rtc_time rtc = {.tm_sec = tm.tm_sec,
                                   .tm_min = tm.tm_min,
                                   .tm_hour = tm.tm_hour,
                                   .tm_mday = tm.tm_mday,
                                   .tm_mon = tm.tm_mon,
                                   .tm_year = tm.tm_year};
            int result = fd < 0 ? -1 : ioctl(fd, RTC_SET_TIME, &rtc);
            if (fd >= 0)
                close(fd);
            if (result)
                return lm_failure("RTC unavailable or write failed");
        } else
            return lm_failure("unknown operation");
    }
    lm_json *out = lm_success();
    lm_set_integer(out, "unix_seconds", time(NULL));
    lm_set_string(out, "timezone", s->zone);
    lm_set_string(out, "server", s->server);
    lm_set_boolean(out, "automatic", s->automatic);
    lm_set_boolean(out, "synchronized", s->synced);
    int rtcfd = d->options.test ? -1 : open("/dev/rtc0", O_RDONLY | O_CLOEXEC);
    struct rtc_time rtc;
    int rtcok = rtcfd >= 0 && !ioctl(rtcfd, RTC_RD_TIME, &rtc);
    if (rtcfd >= 0)
        close(rtcfd);
    lm_set_boolean(out, "rtc_available", rtcok);
    if (rtcok) {
        struct tm value = {.tm_sec = rtc.tm_sec,
                           .tm_min = rtc.tm_min,
                           .tm_hour = rtc.tm_hour,
                           .tm_mday = rtc.tm_mday,
                           .tm_mon = rtc.tm_mon,
                           .tm_year = rtc.tm_year};
        lm_set_integer(out, "rtc_unix_seconds", timegm(&value));
    }
    lm_set_string(out, "error", s->error);
    return out;
}
int timed_run(int argc, char **argv) {
    struct time_state s = {.fd = -1, .automatic = 1, .zone = "Etc/UTC", .server = "162.159.200.1"};
    struct lm_daemon d = {.options = {"/run/leon/time.sock", "/data/leon/system/time.json",
                                      "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0},
                          .data = &s,
                          .handle = handle,
                          .tick = tick};
    if (lm_options(argc, argv, &d.options))
        return 2;
    lm_json *settings = lm_read_json(d.options.state);
    const char *zone = lm_string(lm_get(settings, "timezone"), "Etc/UTC"),
               *server = lm_string(lm_get(settings, "server"), "162.159.200.1");
    struct in_addr addr;
    if (zone_valid(zone) && !apply_zone(&d, zone))
        snprintf(s.zone, sizeof(s.zone), "%s", zone);
    if (inet_pton(AF_INET, server, &addr) == 1)
        snprintf(s.server, sizeof(s.server), "%s", server);
    s.automatic = lm_boolean(lm_get(settings, "automatic"), 1);
    lm_free(settings);
    int result = lm_serve(&d);
    if (s.fd >= 0)
        close(s.fd);
    return result;
}
