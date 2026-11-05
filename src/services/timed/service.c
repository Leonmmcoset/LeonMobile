#include "service.h"
#include "daemon.h"
#include "ntp.h"
#include "time_state.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void store32(unsigned char *p, uint32_t n) {
    n = htonl(n);
    memcpy(p, &n, 4);
}
static int persist(struct lm_daemon *d) {
    struct time_state *s = d->data;
    lm_json *j = lm_object();
    lm_set_boolean(j, "automatic", s->automatic);
    lm_set_string(j, "timezone", s->zone);
    lm_set_string(j, "server", s->server);
    lm_set_integer(j, "last_attempt", s->last_attempt);
    lm_set_integer(j, "last_sync", s->last_sync);
    lm_set_string(j, "last_result", s->result);
    lm_set_string(j, "last_error", s->error);
    int result = lm_write_json(d->options.state, j);
    lm_free(j);
    return result;
}
static int server_valid(const char *text) {
    struct in_addr value;
    if (inet_pton(AF_INET,text,&value) != 1) return 0;
    uint32_t address = ntohl(value.s_addr);
    return address && (address >> 24) < 224;
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
                s->last_sync = target.tv_sec;
                time_write_rtc(s);
                s->error[0] = 0;
            }
            snprintf(s->result, sizeof(s->result), "%s", s->synced ? "success" : "error");
            if (persist(d)) snprintf(s->error, sizeof(s->error), "cannot persist synchronization history");
            close(s->fd);
            s->fd = -1;
            s->next = lm_now_ms() + (s->synced ? 3600000 : 60000);
        } else if (lm_now_ms() >= s->deadline) {
            close(s->fd);
            s->fd = -1;
            s->next = lm_now_ms() + 60000;
            snprintf(s->error, sizeof(s->error), "NTP response timed out");
            snprintf(s->result, sizeof(s->result), "error");persist(d);
        }
        return;
    }
    if (lm_now_ms() < s->next)
        return;
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(123)};
    if (inet_pton(AF_INET, s->server, &peer.sin_addr) != 1)
        return;
    s->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    s->last_attempt = time(NULL);s->synced = 0;
    snprintf(s->result, sizeof(s->result), "pending");s->error[0] = 0;
    if (s->fd < 0) {
        s->next = lm_now_ms() + 60000;
        snprintf(s->result, sizeof(s->result), "error");
        snprintf(s->error, sizeof(s->error), "cannot create SNTP socket");persist(d);
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
        snprintf(s->result, sizeof(s->result), "error");
        snprintf(s->error, sizeof(s->error), "cannot send SNTP request");persist(d);
        return;
    }
    s->deadline = lm_now_ms() + 3000;
}
static lm_json *handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct time_state *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    if (!strcmp(op, "timezones")) return time_zones(r);
    if (strcmp(op, "status")) {
        if (uid != 0 && uid != 11000 && uid != 1000)
            return lm_failure("permission denied");
        if (!strcmp(op, "configure")) {
            struct time_state old = *s;
            lm_json *keys = lm_keys(r);int valid = lm_size(r) > 1;
            for (size_t i = 0; i < lm_size(keys); ++i) {
                const char *key = lm_string(lm_at(keys, i), "");
                if (strcmp(key,"op") && strcmp(key,"timezone") && strcmp(key,"server") && strcmp(key,"automatic")) valid = 0;
            }
            lm_free(keys);
            if (!valid) return lm_failure("unknown or missing time setting");
            if ((lm_get(r, "timezone") && !lm_is_string(lm_get(r, "timezone"))) ||
                (lm_get(r, "server") && !lm_is_string(lm_get(r, "server"))) ||
                (lm_get(r, "automatic") && lm_boolean(lm_get(r, "automatic"), -1) < 0))
                return lm_failure("invalid configuration types");
            const char *zone = lm_string(lm_get(r, "timezone"), old.zone),
                       *server = lm_string(lm_get(r, "server"), old.server);
            int automatic = lm_boolean(lm_get(r, "automatic"), s->automatic);
            if (!time_zone_valid(zone) || !server_valid(server) ||
                time_zone_apply(d, zone))
                return lm_failure("invalid or unavailable timezone/server");
            snprintf(s->zone, sizeof(s->zone), "%s", zone);
            snprintf(s->server, sizeof(s->server), "%s", server);
            s->automatic = automatic;
            s->synced = 0;
            s->error[0] = 0;
            snprintf(s->result, sizeof(s->result), "%s", automatic ? "scheduled" : "disabled");
            if (persist(d)) {
                *s = old;
                time_zone_apply(d, old.zone);
                return lm_failure("cannot persist time settings");
            }
            if (s->fd >= 0)
                close(s->fd);
            s->fd = -1;
            s->next = 0;
        } else if (!strcmp(op, "sync")) {
            if (!s->automatic) return lm_failure("automatic time is disabled");
            if (d->options.test) return lm_failure("network clock mutations disabled for test roots");
            if (s->fd >= 0) close(s->fd);
            s->fd = -1;s->next = 0;s->synced = 0;s->error[0] = 0;
            snprintf(s->result, sizeof(s->result), "scheduled");
        } else if (!strcmp(op, "set_time") || !strcmp(op,"set_local_time")) {
            if (s->automatic) return lm_failure("disable automatic time before setting the clock");
            time_t parsed;
            long long seconds;
            if (!strcmp(op,"set_local_time")) {
                const char *text = lm_string(lm_get(r,"local"), "");
                if (time_local_parse(text,&parsed)) return lm_failure("invalid, nonexistent or ambiguous local date/time");
                seconds = parsed;
            } else seconds = lm_integer(lm_get(r, "unix_seconds"), -1);
            if (seconds < 1577836800LL || seconds > 4102444800LL)
                return lm_failure("time outside 2020..2100");
            if (d->options.test)
                return lm_failure("clock mutations disabled for test roots");
            struct timespec value = {.tv_sec = seconds};
            if (clock_settime(CLOCK_REALTIME, &value))
                return lm_failure(strerror(errno));
            s->synced = 0;
            s->error[0] = 0;snprintf(s->result,sizeof(s->result),"manual");
            time_write_rtc(s);
            if (persist(d)) snprintf(s->error,sizeof(s->error),"clock set; cannot persist time history");
        } else if (!strcmp(op, "write_rtc")) {
            if (d->options.test)
                return lm_failure("RTC writes disabled for test roots");
            time_write_rtc(s);
            if (*s->rtc_error) return lm_failure(s->rtc_error);
        } else
            return lm_failure("unknown operation");
    }
    lm_json *out = lm_success();
    lm_set_integer(out, "unix_seconds", time(NULL));
    lm_set_string(out, "timezone", s->zone);
    lm_set_string(out, "server", s->server);
    lm_set_boolean(out, "automatic", s->automatic);
    lm_set_boolean(out, "synchronized", s->synced);
    lm_set_boolean(out, "sync_pending", s->fd >= 0);
    lm_set_integer(out, "last_attempt", s->last_attempt);
    lm_set_integer(out, "last_sync", s->last_sync);
    lm_set_string(out, "last_result", s->result);
    lm_set_string(out, "rtc_error", s->rtc_error);
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
    struct time_state s = {.fd = -1, .automatic = 1, .zone = "Etc/UTC", .server = "162.159.200.1", .result="never"};
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
    struct stat stored;
    if ((!settings && (!lstat(d.options.state,&stored) || errno != ENOENT)) ||
        (settings && (!lm_is_object(settings) || !lm_is_string(lm_get(settings,"timezone")) ||
          !lm_is_string(lm_get(settings,"server")) || lm_boolean(lm_get(settings,"automatic"),-1)<0)) ||
        !time_zone_valid(zone) || !server_valid(server) || time_zone_apply(&d,zone)) {
        lm_free(settings);fputs("timed: invalid or unavailable time configuration\n",stderr);return 1;
    }
    snprintf(s.zone,sizeof(s.zone),"%s",zone);snprintf(s.server,sizeof(s.server),"%s",server);
    s.automatic = lm_boolean(lm_get(settings, "automatic"), 1);
    s.last_attempt = lm_integer(lm_get(settings,"last_attempt"),0);
    s.last_sync = lm_integer(lm_get(settings,"last_sync"),0);
    snprintf(s.result,sizeof(s.result),"%s",lm_string(lm_get(settings,"last_result"),"never"));
    snprintf(s.error,sizeof(s.error),"%s",lm_string(lm_get(settings,"last_error"),""));
    lm_free(settings);
    int result = lm_serve(&d);
    if (s.fd >= 0)
        close(s.fd);
    return result;
}
