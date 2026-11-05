#include "log.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int leon_log_capture_open(struct leon_log_capture *c) {
    int pair[2];
    if (pipe2(pair, O_CLOEXEC | O_NONBLOCK))
        return -1;
    c->fd = pair[0];
    return pair[1];
}
static void flush(struct leon_log_capture *c, const char *path) {
    while (c->count) {
        if (leon_log_send(path, &c->pending[c->head]))
            return;
        c->head = (c->head + 1) % 4;
        --c->count;
    }
}
static void emit(struct leon_log_capture *c, const char *path, uid_t uid, pid_t pid,
                 enum leon_log_scope scope, const char *source) {
    struct leon_log_packet p = {.magic = LEON_LOG_MAGIC, .uid = uid, .pid = (uint32_t)pid,
                                .level = 6, .scope = scope};
    snprintf(p.source, sizeof(p.source), "%s", source);
    flush(c, path);
    if (c->lost && !c->count) {
        p.level = 4;
        p.length = (uint32_t)snprintf((char *)p.message, sizeof(p.message),
                                    "log transport dropped %llu fragments",
                                    (unsigned long long)c->lost);
        if (!leon_log_send(path, &p))
            c->lost = 0;
    }
    if (c->used) {
        p.level = 6;
        p.length = c->used;
        memcpy(p.message, c->line, c->used);
        if (c->count || leon_log_send(path, &p)) {
            if (c->count < 4) {
                c->pending[(c->head + c->count) % 4] = p;
                ++c->count;
            } else
                ++c->lost;
        }
        c->used = 0;
    }
}
void leon_log_capture_drain(struct leon_log_capture *c, const char *path, uid_t uid, pid_t pid,
                            enum leon_log_scope scope, const char *source) {
    flush(c, path);
    unsigned char buffer[4096];
    /* Per-stream work budget protects readiness, watchdog and lifecycle handling. */
    for (int budget = 0; c->fd >= 0 && budget < 4; ++budget) {
        ssize_t n = read(c->fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) {
            --budget;
            continue;
        }
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n; ++i) {
            if (buffer[i] != '\n')
                c->line[c->used++] = buffer[i];
            if (buffer[i] == '\n' || c->used == sizeof(c->line))
                emit(c, path, uid, pid, scope, source);
        }
    }
    /* Flush prompts and other output without a trailing newline. */
    if (c->used || c->lost)
        emit(c, path, uid, pid, scope, source);
}
void leon_log_capture_close(struct leon_log_capture *c, const char *path, uid_t uid, pid_t pid,
                            enum leon_log_scope scope, const char *source) {
    leon_log_capture_drain(c, path, uid, pid, scope, source);
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
}
