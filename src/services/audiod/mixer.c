#include "audio.h"
#include <string.h>
void audio_render(struct audio_stream *p, void *buffer, size_t bytes) {
    memset(buffer, 0, bytes);
    if (atomic_load(&p->paused) || atomic_load(&p->blocked)) {
        atomic_store(&p->peak, 0);
        return;
    }
    unsigned read = atomic_load_explicit(&p->read_index, memory_order_relaxed);
    unsigned write = atomic_load_explicit(&p->write_index, memory_order_acquire);
    size_t count = write - read;
    if (count > bytes)
        count = bytes;
    count -= count % (p->channels * 2);
    if (count < bytes)
        atomic_fetch_add(&p->underruns, 1);
    unsigned gain = atomic_load(&p->gain), peak = 0;
    unsigned char *out = buffer;
    for (size_t i = 0; i < count; i += 2) {
        unsigned lo = p->ring[(read + i) % AUDIO_RING], hi = p->ring[(read + i + 1) % AUDIO_RING];
        int sample = (int16_t)(lo | (hi << 8));
        sample = sample * (int)gain / 10000;
        unsigned amplitude = (unsigned)(sample < 0 ? -sample : sample);
        if (amplitude > peak)
            peak = amplitude;
        out[i] = (unsigned char)sample;
        out[i + 1] = (unsigned char)((unsigned)sample >> 8);
    }
    atomic_store_explicit(&p->read_index, read + (unsigned)count, memory_order_release);
    atomic_fetch_add(&p->played, count);
    if (count)
        atomic_store(&p->peak, peak);
}
void audio_focus(struct audio *s) {
    unsigned exclusive = 0, duck = 0;
    for (size_t i = 0; i < AUDIO_STREAMS; ++i) {
        struct audio_stream *p = &s->streams[i];
        if (!p->token[0] || atomic_load(&p->paused))
            continue;
        if (p->focus == 1 && p->order > exclusive)
            exclusive = p->order;
        if (p->focus == 2 && p->order > duck)
            duck = p->order;
    }
    for (size_t i = 0; i < AUDIO_STREAMS; ++i) {
        struct audio_stream *p = &s->streams[i];
        if (!p->token[0])
            continue;
        atomic_store(&p->blocked, p->focus == 1 && p->order != exclusive);
        unsigned gain = (unsigned)p->volume * 100;
        if (duck && p->focus != 2)
            gain /= 5;
        if (s->test)
            gain = s->muted ? 0 : gain * (unsigned)s->volume / 100;
        atomic_store(&p->gain, gain);
    }
}
void audio_close(struct audio *s, struct audio_stream *p) {
    audio_backend_close(s, p);
    memset(p, 0, sizeof(*p));
    audio_focus(s);
}
