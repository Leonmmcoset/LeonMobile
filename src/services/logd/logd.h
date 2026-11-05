#pragma once
#include "auth.h"
#include "daemon.h"
#include "log.h"
#include <sys/socket.h>
#include <time.h>
#define LOG_TEXT 8192
#define LOG_SYSTEM_RECORDS 256
#define LOG_USER_RECORDS 128
#define LOG_SEGMENTS 4
#define LOG_SEGMENT_BYTES (1024 * 1024)
struct log_record {
    int64_t seq;
    unsigned size;
    char text[LOG_TEXT];
};
struct log_ack { int64_t seq, bytes, dropped; int error; };
struct log_options {
    const char *runtime, *state, *auth, *crash, *services, *kmsg, *boot;
    int test;
};
struct log_state {
    struct log_options options;
    char boot[37];
    struct log_record system[LOG_SYSTEM_RECORDS], user[LOG_USER_RECORDS];
    unsigned system_count, user_count;
    int writer, kernel;
    pid_t writer_pid;
    int64_t seq, accepted, rate_dropped, invalid, memory_evicted, disk_dropped, kernel_lost;
    int64_t writer_seen;
    struct log_ack ack;
};
int64_t log_clock_ns(clockid_t clock);
int log_secure_directory(const char *path, mode_t mode);
void log_child(int fd, pid_t parent);
void log_append(struct log_state *, unsigned level, const char *scope, const char *source,
                int64_t uid, int64_t pid, const unsigned char *, size_t, int64_t kernel_seq,
                int64_t kernel_usec);
void log_ingest(struct log_state *, int fd, int trusted);
void log_kernel(struct log_state *, int cursor);
void log_writer(const struct log_options *, int fd);
int log_storage_scan(const char *, void (*)(void *, const char *, size_t), void *);
void log_query(const struct log_state *, struct ucred, const char *, size_t, int fd);
