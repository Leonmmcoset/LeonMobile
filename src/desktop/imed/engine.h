#ifndef LEON_IME_ENGINE_H
#define LEON_IME_ENGINE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ime_word {
    char key[49], text[64];
};
struct ime_engine {
    struct ime_word *words;
    size_t count, page;
    char preedit[49];
};
int ime_engine_open(struct ime_engine *, const char *path);
void ime_engine_close(struct ime_engine *);
void ime_engine_clear(struct ime_engine *);
int ime_engine_letter(struct ime_engine *, char c);
void ime_engine_backspace(struct ime_engine *);
size_t ime_engine_candidates(struct ime_engine *, const char **out, size_t limit);
#ifdef __cplusplus
}
#endif
#endif
