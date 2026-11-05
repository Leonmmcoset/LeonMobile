#include "logd.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
int64_t log_clock_ns(clockid_t clock) {
    struct timespec t;
    clock_gettime(clock, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
/* Keep valid UTF-8, but escape terminal controls, invalid bytes and bidi controls.
 * No caller-controlled string reaches a terminal as an executable escape. */
static void sanitize(const unsigned char *in, size_t n, char *out, size_t cap) {
    size_t used = 0;
    for (size_t i = 0; i < n && used + 8 < cap;) {
        unsigned c = in[i], length = 1, cp = c;
        if (c >= 0xc2 && c <= 0xf4) {
            length = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            cp = c & (length == 2 ? 31 : length == 3 ? 15 : 7);
            if (i + length > n)
                length = 0;
            for (unsigned j = 1; j < length; ++j)
                if ((in[i + j] & 0xc0) != 0x80) {
                    length = 0;
                    break;
                } else
                    cp = (cp << 6) | (in[i + j] & 63);
            if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) ||
                (length == 4 && cp < 0x10000) || cp > 0x10ffff ||
                (cp >= 0xd800 && cp <= 0xdfff))
                length = 0;
        } else if (c >= 0x80)
            length = 0;
        if (!length || c < 32 || c == 127 || (cp >= 0x80 && cp <= 0x9f) ||
            cp == 0x061c || cp == 0x200e || cp == 0x200f ||
            (cp >= 0x2028 && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069)) {
            /* Escape every encoded byte, preserving the original evidence. */
            unsigned bytes = length ? length : 1;
            for (unsigned j = 0; j < bytes; ++j)
                used += (size_t)snprintf(out + used, cap - used, "\\x%02x", in[i + j]);
            i += bytes;
        } else {
            memcpy(out + used, in + i, length);
            used += length;
            i += length;
        }
    }
    out[used] = 0;
}
void log_append(struct log_state *s, unsigned level, const char *scope, const char *source,
                int64_t uid, int64_t pid, const unsigned char *bytes, size_t n, int64_t kseq) {
    char message[LEON_LOG_MESSAGE * 4 + 16], tag[LEON_LOG_SOURCE * 4 + 16];
    int truncated = n > LEON_LOG_MESSAGE;
    if (n > LEON_LOG_MESSAGE)
        n = LEON_LOG_MESSAGE;
    sanitize(bytes, n, message, sizeof(message));
    sanitize((const unsigned char *)source, strlen(source), tag, sizeof(tag));
    lm_json *j = lm_object();
    int64_t now = log_clock_ns(CLOCK_BOOTTIME);
    s->seq = now > s->seq ? now : s->seq + 1;
    lm_set_integer(j, "seq", s->seq);
    lm_set_string(j, "boot", s->boot);
    lm_set_integer(j, "realtime_ms", log_clock_ns(CLOCK_REALTIME) / 1000000);
    lm_set_integer(j, "monotonic_ns", now);
    lm_set_integer(j, "level", level);
    lm_set_integer(j, "uid", uid);
    lm_set_integer(j, "pid", pid);
    lm_set_integer(j, "kernel_seq", kseq);
    lm_set_boolean(j, "truncated", truncated);
    lm_set_string(j, "scope", scope);
    lm_set_string(j, "source", tag);
    lm_set_string(j, "message", message);
    char *text = lm_dump(j);
    lm_free(j);
    if (!text)
        return;
    size_t size = strlen(text);
    if (size >= LOG_TEXT) {
        ++s->invalid;
        free(text);
        return;
    }
    int system = !strcmp(scope, "system") || !strcmp(scope, "kernel");
    unsigned *count = system ? &s->system_count : &s->user_count;
    unsigned cap = system ? LOG_SYSTEM_RECORDS : LOG_USER_RECORDS;
    struct log_record *record = &(system ? s->system : s->user)[(*count)++ % cap];
    if (*count > cap)
        ++s->memory_evicted;
    record->seq = s->seq;
    record->size = (unsigned)size;
    memcpy(record->text, text, size + 1);
    ++s->accepted;
    if (s->writer < 0 || send(s->writer, text, size, MSG_DONTWAIT | MSG_NOSIGNAL) != (ssize_t)size)
        ++s->disk_dropped;
    free(text);
}
struct quota { uid_t uid; int64_t time; unsigned tokens; int used; };
static int quota(uid_t uid) {
    static struct quota slots[128];
    int64_t now = log_clock_ns(CLOCK_MONOTONIC) / 1000000;
    struct quota *q = NULL, *empty = NULL;
    for (unsigned i = 0; i < 128; ++i) {
        if (slots[i].used && slots[i].uid == uid)
            q = &slots[i];
        if (!slots[i].used || now - slots[i].time > 60000)
            empty = &slots[i];
    }
    if (!q) {
        if (!empty)
            return 0;
        q = empty;
        *q = (struct quota){.uid = uid, .time = now, .tokens = 128, .used = 1};
    }
    unsigned refill = (unsigned)((now - q->time) / 16);
    if (refill) {
        q->tokens = q->tokens + refill > 128 ? 128 : q->tokens + refill;
        q->time = now;
    }
    if (!q->tokens)
        return 0;
    --q->tokens;
    return 1;
}
void log_ingest(struct log_state *s, int fd, int trusted) {
    for (int budget = 0; budget < (trusted ? 128 : 32); ++budget) {
        struct leon_log_packet p;
        char controls[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int) * 16)];
        struct iovec v = {&p, sizeof(p)};
        struct msghdr m = {.msg_iov = &v, .msg_iovlen = 1,
                           .msg_control = controls, .msg_controllen = sizeof(controls)};
        ssize_t n = recvmsg(fd, &m, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (n < 0)
            break;
        struct ucred peer = {.pid = -1};
        int invalid = m.msg_flags & (MSG_CTRUNC | MSG_TRUNC);
        for (struct cmsghdr *h = CMSG_FIRSTHDR(&m); h; h = CMSG_NXTHDR(&m, h)) {
            if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_CREDENTIALS &&
                h->cmsg_len == CMSG_LEN(sizeof(peer)))
                memcpy(&peer, CMSG_DATA(h), sizeof(peer));
            else {
                invalid = 1;
                if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_RIGHTS)
                    for (size_t i = 0; i < (h->cmsg_len - CMSG_LEN(0)) / sizeof(int); ++i) {
                        int extra;
                        memcpy(&extra, (char *)CMSG_DATA(h) + i * sizeof(int), sizeof(extra));
                        close(extra);
                    }
            }
        }
        if (invalid || n != sizeof(p) || peer.pid <= 0 || p.magic != LEON_LOG_MAGIC ||
            p.length > sizeof(p.message) || p.level > 7 ||
            !memchr(p.source, 0, sizeof(p.source)) || (trusted && peer.uid)) {
            ++s->invalid;
            continue;
        }
        const char *scope = "user";
        char source[LEON_LOG_SOURCE];
        uid_t uid = peer.uid;
        pid_t pid = peer.pid;
        if (trusted && (p.scope == LEON_LOG_SYSTEM || p.scope == LEON_LOG_APP)) {
            scope = p.scope == LEON_LOG_APP ? "app" : "system";
            uid = p.uid;
            pid = (pid_t)p.pid;
            snprintf(source, sizeof(source), "%s", p.source);
        } else {
            /* A tag is user text, never an authoritative application/service name. */
            snprintf(source, sizeof(source), "uid:%u/%.70s", peer.uid, p.source);
        }
        if ((!trusted || !strcmp(scope, "app")) && !quota(uid)) {
            ++s->rate_dropped;
            continue;
        }
        log_append(s, p.level, scope, source, uid, pid, p.message, p.length, -1);
    }
}
void log_kernel(struct log_state *s, int cursor) {
    static uint64_t last;
    static int loaded;
    if (!loaded) {
        char text[128], boot[37];
        ssize_t n = pread(cursor, text, sizeof(text) - 1, 0);
        if (n > 0) {
            text[n] = 0;
            unsigned long long value;
            if (sscanf(text, "%36s %llu", boot, &value) == 2 && !strcmp(boot, s->boot))
                last = value + 1;
        }
        loaded = 1;
    }
    for (int budget = 0; budget < 64; ++budget) {
        char buffer[8192];
        ssize_t n = read(s->kernel, buffer, sizeof(buffer) - 1);
        if (n < 0 && errno == EPIPE) {
            ++s->kernel_lost;
            continue;
        }
        if (n <= 0)
            break;
        buffer[n] = 0;
        unsigned pri;
        unsigned long long seq, usec;
        char *message = strchr(buffer, ';');
        if (!message || sscanf(buffer, "%u,%llu,%llu,", &pri, &seq, &usec) != 3 || seq < last)
            continue;
        ++message;
        char *end = strchr(message, '\n');
        size_t length = end ? (size_t)(end - message) : strlen(message);
        log_append(s, pri & 7, "kernel", "kmsg", -1, -1,
                   (unsigned char *)message, length, (int64_t)seq);
        last = seq + 1;
        char checkpoint[128];
        int count = snprintf(checkpoint, sizeof(checkpoint), "%s %llu\n", s->boot, seq);
        /* Runtime cursor lives in /run (tmpfs), never in the persistent journal. */
        if (cursor >= 0) {
            (void)pwrite(cursor, checkpoint, (size_t)count, 0);
            (void)ftruncate(cursor, count);
        }
    }
}
