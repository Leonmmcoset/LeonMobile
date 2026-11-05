#ifndef LEON_TIME_STATE_H
#define LEON_TIME_STATE_H
#include "daemon.h"
#include <time.h>
struct time_state {
    int fd, automatic, synced;
    long long next, deadline, last_attempt, last_sync;
    unsigned char request[48];
    struct timespec sent;
    char zone[128], server[64], error[128], result[24], rtc_error[128];
};
int time_zone_valid(const char *zone);
int time_zone_apply(struct lm_daemon *d, const char *zone);
lm_json *time_zones(const lm_json *request);
int time_local_parse(const char *text, time_t *seconds);
void time_write_rtc(struct time_state *state);
#endif
