#include "ntp.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
static uint32_t load32(const unsigned char *p) {
    uint32_t n;
    memcpy(&n, p, 4);
    return ntohl(n);
}
static double timestamp(const unsigned char *p, time_t reference) {
    int64_t seconds = (int64_t)load32(p) - 2208988800LL;
    while (seconds - (int64_t)reference > 2147483647LL)
        seconds -= 4294967296LL;
    while ((int64_t)reference - seconds > 2147483647LL)
        seconds += 4294967296LL;
    return (double)seconds + (double)load32(p + 4) / 4294967296.0;
}
int leon_ntp_sample(const unsigned char *p, size_t n, const unsigned char request[48],
                    struct timespec sent, struct timespec received, struct timespec *target) {
    if (n < 48) return -1;
    unsigned version = (p[0] >> 3) & 7;
    if (n < 48 || (p[0] & 7) != 4 || (p[0] >> 6) == 3 || (version != 3 && version != 4) ||
        p[1] < 1 || p[1] > 15 || memcmp(p + 24, request + 40, 8) ||
        (!load32(p + 32) && !load32(p + 36)) || (!load32(p + 40) && !load32(p + 44)))
        return -1;
    double t1 = (double)sent.tv_sec + sent.tv_nsec / 1e9,
           t4 = (double)received.tv_sec + received.tv_nsec / 1e9;
    double t2 = timestamp(p + 32, received.tv_sec), t3 = timestamp(p + 40, received.tv_sec);
    double delay = (t4 - t1) - (t3 - t2), offset = ((t2 - t1) + (t3 - t4)) / 2;
    if (t4 < t1 || t4 - t1 > 3.0 || t3 < t2 || delay < -.002 || delay > 2.0 || offset > 86400 ||
        offset < -86400)
        return -1;
    double value = t4 + offset;
    target->tv_sec = (time_t)value;
    target->tv_nsec = (long)((value - target->tv_sec) * 1e9);
    return 0;
}
