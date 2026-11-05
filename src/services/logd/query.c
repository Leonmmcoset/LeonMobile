#include "logd.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct selection {
    const struct log_state *state;
    struct ucred peer;
    int privileged, previous, tail, limit, max_level, count, more;
    int64_t after, uid, pid, oldest;
    const char *source;
    char boot[37];
    lm_json *records[128];
};
static int64_t number(const lm_json *j, const char *key, int64_t fallback) {
    return lm_integer(lm_get(j, key), fallback);
}
static const char *string(const lm_json *j, const char *key) {
    return lm_string(lm_get(j, key), "");
}
static void reset(struct selection *s) {
    for (int i = 0; i < s->count; ++i)
        lm_free(s->records[i]);
    s->count = s->more = 0;
    s->oldest = 0;
}
static void select_record(void *context, const char *text, size_t size) {
    struct selection *s = context;
    lm_json *j = lm_parse(text, size);
    int64_t seq = number(j, "seq", -1), uid = number(j, "uid", -2);
    const char *boot = string(j, "boot"), *scope = string(j, "scope");
    if (!lm_is_object(j) || seq < 0 || strlen(boot) != 36 ||
        !lm_is_string(lm_get(j, "message")) || !lm_is_string(lm_get(j, "source")))
        goto discard;
    if (s->previous && strcmp(boot, s->state->boot) && strcmp(boot, s->boot)) {
        reset(s);
        strcpy(s->boot, boot);
    }
    if (strcmp(boot, s->boot) ||
        (!s->privileged && (uid != s->peer.uid ||
                            (strcmp(scope, "app") && strcmp(scope, "user")))))
        goto discard;
    if (!s->oldest || seq < s->oldest)
        s->oldest = seq;
    if (seq <= s->after || number(j, "level", 8) > s->max_level ||
        (s->uid >= 0 && uid != s->uid) ||
        (s->pid >= 0 && number(j, "pid", -1) != s->pid) ||
        (*s->source && strcmp(s->source, string(j, "source"))))
        goto discard;
    int at = 0;
    while (at < s->count && number(s->records[at], "seq", 0) < seq)
        ++at;
    if (at < s->count && number(s->records[at], "seq", 0) == seq)
        goto discard;
    if (s->count == s->limit) {
        s->more = 1;
        if ((!s->tail && at == s->count) || (s->tail && !at))
            goto discard;
        int remove = s->tail ? 0 : s->count - 1;
        lm_free(s->records[remove]);
        memmove(s->records + remove, s->records + remove + 1,
                (size_t)(s->count - remove - 1) * sizeof(*s->records));
        --s->count;
        if (s->tail)
            --at;
    }
    memmove(s->records + at + 1, s->records + at,
            (size_t)(s->count - at) * sizeof(*s->records));
    s->records[at] = j;
    ++s->count;
    return;
discard:
    lm_free(j);
}
static int settings_peer(const struct log_state *s, struct ucred peer) {
    return peer.uid == 11000 && (s->options.test ||
           auth_peer(peer.pid, peer.uid, 11000, "/usr/bin/leon-settings", "/usr/sbin/leon-launchd"));
}
static int unlocked(const struct log_state *s, struct auth_state *state) {
    char response[LEON_AUTH_LIMIT];
    return !auth_exchange(s->options.auth, "{\"op\":\"status\"}\n", response, 150) &&
           !auth_decode(response, state) && state->ok && !state->locked && *state->epoch;
}
static lm_json *status(const struct log_state *s, int privileged) {
    lm_json *j = lm_success();
    lm_set_string(j, "boot", s->boot);
    lm_set_string(j, "access", privileged ? "system" : "own");
    lm_set_integer(j, "cursor", s->seq);
    lm_set_integer(j, "memory_limit_records", LOG_SYSTEM_RECORDS + LOG_USER_RECORDS);
    lm_set_integer(j, "disk_limit_bytes", LOG_SEGMENT_BYTES * LOG_SEGMENTS);
    if (privileged) {
        lm_set_integer(j, "accepted", s->accepted);
        lm_set_integer(j, "rate_dropped", s->rate_dropped);
        lm_set_integer(j, "invalid", s->invalid);
        lm_set_integer(j, "memory_evicted", s->memory_evicted);
        lm_set_integer(j, "disk_queue_dropped", s->disk_dropped);
        lm_set_integer(j, "disk_write_dropped", s->ack.dropped);
        lm_set_integer(j, "writer_error", s->ack.error);
        lm_set_integer(j, "writer_seq", s->ack.seq);
        lm_set_integer(j, "kernel_lost", s->kernel_lost);
        lm_set_boolean(j, "kernel_available", s->kernel >= 0);
        lm_set_boolean(j, "storage_healthy", s->writer_pid > 0 && !s->ack.error &&
                       s->writer_seen > 0 &&
                       log_clock_ns(CLOCK_MONOTONIC) / 1000000 - s->writer_seen < 2000);
    }
    return j;
}
static lm_json *query(const struct log_state *state, struct ucred peer, const lm_json *request,
                       int privileged) {
    struct selection s = {.state = state, .peer = peer, .privileged = privileged,
                           .limit = (int)number(request, "limit", 50),
                           .max_level = (int)number(request, "max_level", 7),
                           .after = number(request, "after", 0),
                           .uid = number(request, "uid", -1), .pid = number(request, "pid", -1),
                           .source = string(request, "source"),
                           .tail = lm_boolean(lm_get(request, "tail"), 0)};
    const char *boot = lm_string(lm_get(request, "boot"), "current");
    if (number(request, "limit", 50) < 1 || number(request, "limit", 50) > 128 ||
        number(request, "max_level", 7) < 0 || number(request, "max_level", 7) > 7 || s.after < 0 ||
        s.uid < -1 || s.pid < -1 || strlen(s.source) > 400)
        return lm_failure("invalid log filter");
    if (!strcmp(boot, "current"))
        strcpy(s.boot, state->boot);
    else if (!strcmp(boot, "previous"))
        s.previous = 1;
    else if (strlen(boot) == 36)
        strcpy(s.boot, boot);
    else
        return lm_failure("invalid boot selector");
    int degraded = log_storage_scan(state->options.state, select_record, &s);
    unsigned count = state->system_count < LOG_SYSTEM_RECORDS ? state->system_count : LOG_SYSTEM_RECORDS;
    for (unsigned i = 0; i < count; ++i)
        select_record(&s, state->system[i].text, state->system[i].size);
    count = state->user_count < LOG_USER_RECORDS ? state->user_count : LOG_USER_RECORDS;
    for (unsigned i = 0; i < count; ++i)
        select_record(&s, state->user[i].text, state->user[i].size);
    lm_json *j = status(state, privileged), *records = lm_array();
    int64_t next = s.count ? number(s.records[s.count - 1], "seq", 0) : s.after;
    for (int i = 0; i < s.count; ++i)
        lm_append(records, s.records[i]);
    lm_set(j, "records", records);
    lm_set_string(j, "selected_boot", s.boot);
    lm_set_integer(j, "next", next);
    lm_set_boolean(j, "more", s.more && !s.tail);
    lm_set_boolean(j, "tail_limited", s.more && s.tail);
    lm_set_boolean(j, "retention_limited", 1);
    lm_set_boolean(j, "gap", s.after > 0 && s.oldest > s.after);
    lm_set_boolean(j, "history_unavailable", degraded);
    return j;
}
void log_query(const struct log_state *s, struct ucred peer, const char *text, size_t size, int fd) {
    lm_json *request = lm_parse(text, size), *response;
    struct auth_state before = {0}, after = {0};
    int privileged = !peer.uid;
    if (!privileged && settings_peer(s, peer) && unlocked(s, &before))
        privileged = 1;
    const char *op = string(request, "op");
    if (!lm_is_object(request))
        response = lm_failure("invalid JSON object");
    else if (!strcmp(op, "query"))
        response = query(s, peer, request, privileged);
    else if (!strcmp(op, "status"))
        response = status(s, privileged);
    else if (!strcmp(op, "crashes") && privileged) {
        lm_json *r = lm_object();
        lm_set_string(r, "op", "list");
        response = lm_rpc(s->options.crash, r);
        lm_free(r);
        if (!response)
            response = lm_failure("crash service unavailable");
    } else
        response = lm_failure("operation unavailable or not authorized");
    /* Disk reads and crash RPC may overlap a lock or authd restart. Recheck the
     * exact executable, auth epoch and generation before releasing system data. */
    if (privileged && peer.uid &&
        (!settings_peer(s, peer) || !unlocked(s, &after) ||
         strcmp(before.epoch, after.epoch) || before.generation != after.generation)) {
        lm_free(response);
        response = lm_failure("device locked or authentication changed");
    }
    lm_free(request);
    char *out = lm_dump(response);
    lm_free(response);
    if (out) {
        size_t sent = 0, total = strlen(out);
        int64_t deadline = log_clock_ns(CLOCK_MONOTONIC) / 1000000 + 1000;
        while (sent < total && total < LEON_LOG_RESPONSE) {
            int64_t left = deadline - log_clock_ns(CLOCK_MONOTONIC) / 1000000;
            if (left <= 0)
                break;
            struct pollfd p = {fd, POLLOUT, 0};
            if (poll(&p, 1, (int)left) <= 0)
                break;
            ssize_t n = send(fd, out + sent, total - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n < 0 && (errno == EAGAIN || errno == EINTR))
                continue;
            if (n <= 0)
                break;
            sent += (size_t)n;
        }
        free(out);
    }
}
