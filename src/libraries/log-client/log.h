#ifndef LEON_LOG_H
#define LEON_LOG_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LEON_LOG_DIRECTORY "/run/leon-log"
#define LEON_LOG_SOCKET LEON_LOG_DIRECTORY "/control.sock"
#define LEON_LOG_INGEST LEON_LOG_DIRECTORY "/write.sock"
#define LEON_LOG_CAPTURE LEON_LOG_DIRECTORY "/capture.sock"
#define LEON_LOG_MESSAGE 1024
#define LEON_LOG_SOURCE 96
#define LEON_LOG_RESPONSE (768 * 1024)
#define LEON_LOG_MAGIC 0x4c4d4c31u
enum leon_log_scope { LEON_LOG_USER, LEON_LOG_SYSTEM, LEON_LOG_APP };
/* Local v1 datagram ABI: six native-endian uint32 words, source, raw bytes.
 * Public writers cannot choose credentials or scope. capture.sock is root-only. */
struct leon_log_packet {
    uint32_t magic, uid, pid, level, scope, length;
    char source[LEON_LOG_SOURCE];
    unsigned char message[LEON_LOG_MESSAGE];
};
int leon_log_write(const char *path, unsigned level, const char *tag, const void *, size_t);
int leon_log_send(const char *path, const struct leon_log_packet *);
/* Verifies the query server's kernel UID is root. Deadline covers all I/O. */
int leon_log_exchange(const char *path, const char *request, char *response, size_t capacity,
                      unsigned timeout_ms);
/* Both pipe ends are nonblocking. Supervisors always drain, even if logd is down. */
struct leon_log_capture {
    int fd;
    unsigned used;
    uint64_t lost;
    unsigned char line[LEON_LOG_MESSAGE];
};
int leon_log_capture_open(struct leon_log_capture *);
void leon_log_capture_drain(struct leon_log_capture *, const char *, uid_t, pid_t,
                            enum leon_log_scope, const char *);
void leon_log_capture_close(struct leon_log_capture *, const char *, uid_t, pid_t,
                            enum leon_log_scope, const char *);
#ifdef __cplusplus
}
#endif
#endif
