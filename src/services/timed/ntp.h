#ifndef LEON_NTP_H
#define LEON_NTP_H
#include <stddef.h>
#include <time.h>
/* RFC 5905 four-timestamp sample; accepts only bounded, correlated server replies.
 * This SNTP transport is unauthenticated. It never authorizes large clock steps. */
int leon_ntp_sample(const unsigned char *packet, size_t size, const unsigned char request[48],
                    struct timespec sent, struct timespec received, struct timespec *target);
#endif
