#include "ntp.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
static void stamp(unsigned char *p, uint32_t sec, uint32_t fraction) {
    uint32_t v = htonl(sec);
    memcpy(p, &v, 4);
    v = htonl(fraction);
    memcpy(p + 4, &v, 4);
}
int main(void) {
    unsigned char p[48] = {0}, request[48] = {0};
    struct timespec target;
    struct timespec sent = {1700000000, 0}, now = {1700000000, 200000000};
    p[0] = 0x24;
    p[1] = 2;
    stamp(request + 40, 3908988800U, 0x1234);
    memcpy(p + 24, request + 40, 8);
    stamp(p + 32, 3908988800U, 0x10000000);
    stamp(p + 40, 3908988800U, 0x20000000);
    assert(!leon_ntp_sample(p, 48, request, sent, now, &target));
    assert(target.tv_sec == 1700000000 && target.tv_nsec > 190000000 && target.tv_nsec < 195000000);
    p[24] ^= 1;
    assert(leon_ntp_sample(p, 48, request, sent, now, &target));
    p[24] ^= 1;
    p[0] |= 0xc0;
    assert(leon_ntp_sample(p, 48, request, sent, now, &target));
    p[0] = 0x24;
    p[1] = 0;
    assert(leon_ntp_sample(p, 48, request, sent, now, &target));
    p[1] = 2;
    now.tv_sec += 4;
    assert(leon_ntp_sample(p, 48, request, sent, now, &target));
    now.tv_sec -= 4;
    stamp(p + 40, 3909188800U, 0);
    assert(leon_ntp_sample(p, 48, request, sent, now, &target));
    // Era rollover after 2036: a small wire seconds field must not become 1900.
    sent.tv_sec = now.tv_sec = 2208988800LL;
    now.tv_nsec = 200000000;
    stamp(p + 32, (uint32_t)(2208988800ULL + 2208988800ULL), 0x10000000);
    stamp(p + 40, (uint32_t)(2208988800ULL + 2208988800ULL), 0x20000000);
    assert(!leon_ntp_sample(p, 48, request, sent, now, &target));
    assert(target.tv_sec == 2208988800LL);
    return 0;
}
