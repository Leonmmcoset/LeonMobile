#ifndef LEON_AUDIOD_H
#define LEON_AUDIOD_H
#include "capability.h"
#include <stdatomic.h>
#define AUDIO_STREAMS 16
#define AUDIO_RING 65536
struct pw_stream;
struct audio_backend;
struct audio_stream {
    char token[33], app[97], installation[33], title[161], artist[97];
    uid_t uid;
    unsigned rate, channels, focus, order;
    long long expires;
    int volume;
    struct pw_stream *stream;
    atomic_uint read_index, write_index, gain, paused, blocked, peak;
    atomic_ullong played, underruns;
    unsigned char ring[AUDIO_RING];
    _Alignas(max_align_t) unsigned char listener[128];
};
struct audio {
    struct audio_stream streams[AUDIO_STREAMS];
    struct audio_backend *backend;
    lm_json *volumes;
    unsigned order;
    int volume, muted, test;
    long long sample, retry;
    char error[160];
};
struct audio_backend *audio_backend_new(struct audio *s);
void audio_backend_free(struct audio_backend *b);
int audio_backend_ready(struct audio_backend *b);
int audio_backend_open(struct audio *s, struct audio_stream *p);
void audio_backend_close(struct audio *s, struct audio_stream *p);
void audio_backend_master(struct audio *s);
int audio_backend_route(struct audio *s, unsigned id);
lm_json *audio_backend_devices(struct audio *s);
void audio_render(struct audio_stream *p, void *buffer, size_t bytes);
void audio_focus(struct audio *s);
void audio_close(struct audio *s, struct audio_stream *p);
void audio_tick(struct lm_daemon *d);
lm_json *audio_handle(struct lm_daemon *d, uid_t uid, const lm_json *r);
#endif
