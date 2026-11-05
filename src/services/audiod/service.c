#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int save(struct lm_daemon *d, const lm_json *volumes, int muted) {
    lm_json *j = lm_object();
    lm_set_integer(j, "format", 1);
    lm_set_boolean(j, "muted", muted);
    lm_set(j, "apps", lm_copy(volumes));
    int result = lm_write_json(d->options.state, j);
    lm_free(j);
    return result;
}
static lm_json *setting(struct lm_daemon *d, lm_json *r) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.sock", d->options.test ? d->options.root : "/run/leon",
             d->options.test ? "settingsd" : "settings");
    lm_json *out = lm_rpc_bounded(path, r, 300);
    lm_free(r);
    return out;
}
static lm_json *describe(const struct audio_stream *p) {
    lm_json *j = lm_object();
    lm_set_string(j, "token", p->token);
    lm_set_string(j, "app", p->app);
    lm_set_string(j, "title", p->title);
    lm_set_string(j, "artist", p->artist);
    lm_set_boolean(j, "paused", atomic_load(&p->paused));
    lm_set_boolean(j, "focus_lost", atomic_load(&p->blocked));
    lm_set_string(j, "focus", p->focus == 0 ? "mix" : p->focus == 1 ? "exclusive" : "duck");
    lm_set_integer(j, "volume", p->volume);
    lm_set_integer(j, "rate", p->rate);
    lm_set_integer(j, "channels", p->channels);
    lm_set_integer(j, "played_bytes", (int64_t)atomic_load(&p->played));
    lm_set_integer(j, "buffered_bytes", atomic_load(&p->write_index) - atomic_load(&p->read_index));
    lm_set_integer(j, "peak", atomic_load(&p->peak));
    lm_set_integer(j, "underruns", (int64_t)atomic_load(&p->underruns));
    return j;
}
void audio_tick(struct lm_daemon *d) {
    struct audio *s = d->data;
    long long now = lm_now_ms();
    if (!s->test && now >= s->retry && !audio_backend_ready(s->backend)) {
        for (size_t i = 0; i < AUDIO_STREAMS; ++i)
            if (s->streams[i].token[0])
                audio_close(s, &s->streams[i]);
        audio_backend_free(s->backend);
        s->backend = audio_backend_new(s);
        s->retry = now + 2000;
    }
    for (size_t i = 0; i < AUDIO_STREAMS; ++i) {
        struct audio_stream *p = &s->streams[i];
        if (!p->token[0])
            continue;
        if (now >= p->expires || !lm_identity_matches(d, p->uid, p->app, p->installation)) {
            audio_close(s, p);
            continue;
        }
        if (s->test) {
            unsigned char buffer[4096];
            audio_render(p, buffer, sizeof(buffer));
        }
    }
    if (now >= s->sample) {
        s->sample = now + 1000;
        lm_json *r = lm_object();
        lm_set_string(r, "op", "status");
        lm_json *out = setting(d, r);
        int64_t volume = lm_integer(lm_get(lm_get(out, "settings"), "volume"), s->volume);
        if (volume >= 0 && volume <= 100)
            s->volume = (int)volume;
        lm_free(out);
        audio_backend_master(s);
        audio_focus(s);
    }
}
static int short_text(const lm_json *v, size_t max) {
    const char *s = lm_string(v, NULL);
    if (!s || strlen(s) > max)
        return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
        if (*p < 32 || *p == 127)
            return 0;
    return 1;
}
lm_json *audio_handle(struct lm_daemon *d, uid_t uid, const lm_json *r) {
    struct audio *s = d->data;
    const char *op = lm_string(lm_get(r, "op"), "");
    int trusted = lm_trusted(uid) || uid == 11000;
    if (!strcmp(op, "status")) {
        lm_json *out = lm_success(), *streams = lm_array();
        lm_set_boolean(out, "backend_ready", s->test || audio_backend_ready(s->backend));
        lm_set_boolean(out, "test_backend", s->test);
        lm_set_integer(out, "volume", s->volume);
        lm_set_boolean(out, "muted", s->muted);
        lm_set(out, "devices", audio_backend_devices(s));
        const struct audio_stream *current = NULL;
        for (size_t i = 0; i < AUDIO_STREAMS; ++i)
            if (s->streams[i].token[0]) {
                struct audio_stream *p = &s->streams[i];
                if (trusted || uid == p->uid)
                    lm_append(streams, describe(p));
                if (p->focus != 2 && (!current || p->order > current->order))
                    current = p;
            }
        lm_set(out, "streams", streams);
        if (trusted && current)
            lm_set(out, "current", describe(current));
        return out;
    }
    if (!strcmp(op, "set_master")) {
        if (!trusted)
            return lm_failure("permission denied");
        if (!!lm_get(r, "volume") + !!lm_get(r, "muted") != 1)
            return lm_failure("change volume or muted separately");
        int64_t volume = lm_integer(lm_get(r, "volume"), s->volume);
        int muted = lm_boolean(lm_get(r, "muted"), s->muted);
        if (volume < 0 || volume > 100 ||
            (lm_get(r, "volume") && lm_integer(lm_get(r, "volume"), -1) < 0) ||
            (lm_get(r, "muted") && lm_boolean(lm_get(r, "muted"), -1) < 0))
            return lm_failure("invalid volume or mute value");
        if (lm_get(r, "muted") && save(d, s->volumes, muted))
            return lm_failure("cannot persist mute policy");
        if (lm_get(r, "volume")) {
            lm_json *request = lm_object();
            lm_set_string(request, "op", "set");
            lm_set_string(request, "key", "volume");
            lm_set_integer(request, "value", volume);
            lm_json *out = setting(d, request);
            int ok = lm_boolean(lm_get(out, "ok"), 0);
            lm_free(out);
            if (!ok)
                return lm_failure("settings service unavailable");
        }
        s->volume = (int)volume;
        s->muted = muted;
        audio_backend_master(s);
        audio_focus(s);
        return lm_success();
    }
    if (!strcmp(op, "route")) {
        int64_t id = lm_integer(lm_get(r, "device"), -1);
        if (!trusted)
            return lm_failure("permission denied");
        if (id < 1 || id > UINT32_MAX || audio_backend_route(s, (unsigned)id) < 0)
            return lm_failure("output device unavailable");
        return lm_success();
    }
    if (!strcmp(op, "set_volume") || !strcmp(op, "reset_app")) {
        const char *id = lm_string(lm_get(r, "app"), "");
        lm_json *app = trusted && *id ? lm_app_by_id(d->options.root, d->options.builtin, id)
                                      : lm_app_identity(d->options.root, d->options.builtin, uid);
        if (!app || (!trusted && *id && strcmp(id, lm_string(lm_get(app, "id"), "")))) {
            lm_free(app);
            return lm_failure("permission denied");
        }
        id = lm_string(lm_get(app, "id"), "");
        if (!strcmp(op, "reset_app") && uid) {
            lm_free(app);
            return lm_failure("permission denied");
        }
        int64_t volume = lm_integer(lm_get(r, "volume"), -1);
        if (strcmp(op, "reset_app") && (volume < 0 || volume > 100)) {
            lm_free(app);
            return lm_failure("volume must be in 0..100");
        }
        lm_json *next = lm_copy(s->volumes);
        if (!strcmp(op, "reset_app"))
            lm_erase(next, id);
        else
            lm_set_integer(next, id, volume);
        if (lm_size(next) > 512 || save(d, next, s->muted)) {
            lm_free(next);
            lm_free(app);
            return lm_failure("cannot persist application audio policy");
        }
        lm_free(s->volumes);
        s->volumes = next;
        for (size_t i = 0; i < AUDIO_STREAMS; ++i)
            if (s->streams[i].token[0] && !strcmp(s->streams[i].app, id)) {
                if (!strcmp(op, "reset_app"))
                    audio_close(s, &s->streams[i]);
                else
                    s->streams[i].volume = (int)volume;
            }
        lm_free(app);
        audio_focus(s);
        return lm_success();
    }
    if (!strcmp(op, "open")) {
        lm_json *app = lm_app_identity(d->options.root, d->options.builtin, uid);
        if (!app)
            return lm_failure("unknown application identity");
        int64_t rate = lm_integer(lm_get(r, "rate"), 48000),
                channels = lm_integer(lm_get(r, "channels"), 2);
        const char *mode = lm_string(lm_get(r, "focus"), "exclusive");
        if ((strcmp(mode, "mix") && strcmp(mode, "exclusive") && strcmp(mode, "duck")) ||
            rate < 8000 || rate > 96000 || channels < 1 || channels > 2 ||
            (lm_get(r, "rate") && lm_integer(lm_get(r, "rate"), -1) < 0) ||
            (lm_get(r, "channels") && lm_integer(lm_get(r, "channels"), -1) < 0) ||
            (lm_get(r, "title") && !short_text(lm_get(r, "title"), 160)) ||
            (lm_get(r, "artist") && !short_text(lm_get(r, "artist"), 96)) ||
            (lm_get(r, "paused") && lm_boolean(lm_get(r, "paused"), -1) < 0)) {
            lm_free(app);
            return lm_failure("invalid PCM format, focus or metadata");
        }
        lm_json *devices = audio_backend_devices(s);
        int available = s->test || (audio_backend_ready(s->backend) && lm_size(devices));
        lm_free(devices);
        if (!available) {
            lm_free(app);
            return lm_failure("audio output unavailable");
        }
        size_t slot = AUDIO_STREAMS, count = 0;
        for (size_t i = 0; i < AUDIO_STREAMS; ++i)
            if (!s->streams[i].token[0])
                slot = i;
            else
                count += s->streams[i].uid == uid;
        if (slot == AUDIO_STREAMS || count >= 2 || s->order == UINT32_MAX) {
            lm_free(app);
            return lm_failure("audio stream quota reached");
        }
        struct audio_stream *p = &s->streams[slot];
        memset(p, 0, sizeof(*p));
        if (lm_token(p->token)) {
            lm_free(app);
            return lm_failure("random source unavailable");
        }
        snprintf(p->app, sizeof(p->app), "%s", lm_string(lm_get(app, "id"), ""));
        snprintf(p->installation, sizeof(p->installation), "%s",
                 lm_string(lm_get(app, "installation"), ""));
        lm_free(app);
        snprintf(p->title, sizeof(p->title), "%s", lm_string(lm_get(r, "title"), p->app));
        snprintf(p->artist, sizeof(p->artist), "%s", lm_string(lm_get(r, "artist"), ""));
        p->uid = uid;
        atomic_store(&p->paused, lm_boolean(lm_get(r, "paused"), 0));
        p->rate = (unsigned)rate;
        p->channels = (unsigned)channels;
        p->expires = lm_now_ms() + 15000;
        p->focus = !strcmp(mode, "mix") ? 0 : !strcmp(mode, "exclusive") ? 1 : 2;
        p->order = ++s->order;
        p->volume = (int)lm_integer(lm_get(s->volumes, p->app), 100);
        audio_focus(s);
        if (audio_backend_open(s, p) < 0) {
            audio_close(s, p);
            return lm_failure("cannot connect playback stream");
        }
        lm_json *out = lm_success();
        lm_set_string(out, "token", p->token);
        return out;
    }
    const char *token = lm_string(lm_get(r, "token"), "");
    struct audio_stream *p = NULL;
    for (size_t i = 0; i < AUDIO_STREAMS; ++i)
        if (*token && !strcmp(s->streams[i].token, token))
            p = &s->streams[i];
    if (!p || lm_now_ms() >= p->expires || !lm_identity_matches(d, p->uid, p->app, p->installation))
        return lm_failure("unknown or expired stream");
    if (uid != p->uid && !trusted)
        return lm_failure("permission denied");
    p->expires = lm_now_ms() + 15000;
    if (!strcmp(op, "keepalive"))
        return lm_success();
    if (!strcmp(op, "stream")) {
        lm_json *out = lm_success();
        lm_set(out, "stream", describe(p));
        return out;
    }
    if (!strcmp(op, "close")) {
        audio_close(s, p);
        return lm_success();
    }
    if (!strcmp(op, "pause") || !strcmp(op, "resume") || !strcmp(op, "toggle")) {
        int pause = !strcmp(op, "toggle") ? !atomic_load(&p->paused) : !strcmp(op, "pause");
        if (!pause) {
            if (s->order == UINT32_MAX)
                return lm_failure("focus counter exhausted");
            p->order = ++s->order;
        }
        atomic_store(&p->paused, pause);
        audio_focus(s);
        return lm_success();
    }
    if (strcmp(op, "write"))
        return lm_failure("unknown operation");
    if (uid != p->uid)
        return lm_failure("only the stream owner can submit samples");
    unsigned char data[2048];
    const char *hex = lm_string(lm_get(r, "hex"), NULL);
    int bytes = hex ? lm_hex_decode(hex, data, sizeof(data)) : -1;
    if (bytes < 0 || bytes % (p->channels * 2))
        return lm_failure("expected frame-aligned s16le hex PCM (maximum 2048 bytes)");
    unsigned write = atomic_load_explicit(&p->write_index, memory_order_relaxed),
             read = atomic_load_explicit(&p->read_index, memory_order_acquire);
    size_t free_bytes = AUDIO_RING - (write - read);
    if (atomic_load(&p->blocked) && !atomic_load(&p->paused))
        free_bytes = 0;
    if (free_bytes < (unsigned)bytes)
        bytes = (int)(free_bytes - free_bytes % (p->channels * 2));
    for (int i = 0; i < bytes; ++i)
        p->ring[(write + (unsigned)i) % AUDIO_RING] = data[i];
    atomic_store_explicit(&p->write_index, write + (unsigned)bytes, memory_order_release);
    lm_json *out = lm_success();
    lm_set_integer(out, "accepted", bytes);
    return out;
}
