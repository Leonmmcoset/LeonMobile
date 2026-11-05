#include "time_state.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
int time_local_parse(const char *text, time_t *seconds) {
    if (strlen(text) != 19) return -1;
    struct tm value = {0};
    char *end = strptime(text, "%Y-%m-%d %H:%M:%S", &value);
    if (!end || *end || value.tm_sec > 59) return -1;
    value.tm_isdst = -1;
    time_t result = mktime(&value);char normalized[32];
    strftime(normalized, sizeof(normalized), "%Y-%m-%d %H:%M:%S", &value);
    if (strcmp(text, normalized) || result < 1577836800LL || result > 4102444800LL) return -1;
    // A repeated wall-clock time during a DST transition has two meanings.
    struct tm first = value, second = value;
    first.tm_isdst = 0;second.tm_isdst = 1;
    time_t a = mktime(&first), b = mktime(&second);
    char one[32], two[32];
    strftime(one, sizeof(one), "%Y-%m-%d %H:%M:%S", &first);
    strftime(two, sizeof(two), "%Y-%m-%d %H:%M:%S", &second);
    if (a != (time_t)-1 && b != (time_t)-1 && a != b && !strcmp(one, text) && !strcmp(two, text)) return -1;
    *seconds = result;return 0;
}
void time_write_rtc(struct time_state *s) {
    int fd = open("/dev/rtc0", O_RDWR | O_CLOEXEC);time_t now = time(NULL);struct tm tm;
    gmtime_r(&now, &tm);
    struct rtc_time rtc = {.tm_sec=tm.tm_sec,.tm_min=tm.tm_min,.tm_hour=tm.tm_hour,
                           .tm_mday=tm.tm_mday,.tm_mon=tm.tm_mon,.tm_year=tm.tm_year};
    int result = fd < 0 ? -1 : ioctl(fd, RTC_SET_TIME, &rtc);
    if (fd >= 0) close(fd);
    snprintf(s->rtc_error, sizeof(s->rtc_error), "%s", result ? "RTC unavailable or write failed" : "");
}
