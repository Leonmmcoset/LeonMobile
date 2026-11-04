#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ime_engine_open(struct ime_engine *e, const char *path) {
    FILE *file = fopen(path, "r");
    if (!file)
        return -1;
    size_t capacity = 1024;
    e->words = calloc(capacity, sizeof(*e->words));
    if (!e->words) {
        fclose(file);
        return -1;
    }
    char line[128];
    while (fgets(line, sizeof(line), file)) {
        char *tab = strchr(line, '\t'), *newline = strchr(line, '\n');
        if (!tab || !newline || tab == line || tab - line > 48 || newline - tab > 63 ||
            e->count >= 200000)
            goto invalid;
        *tab++ = 0;
        *newline = 0;
        for (const char *p = line; *p; ++p)
            if (*p < 'a' || *p > 'z')
                goto invalid;
        if (!*tab || (e->count && strcmp(e->words[e->count - 1].key, line) > 0))
            goto invalid;
        if (e->count == capacity) {
            capacity *= 2;
            void *next = realloc(e->words, capacity * sizeof(*e->words));
            if (!next)
                goto invalid;
            e->words = next;
        }
        strcpy(e->words[e->count].key, line);
        strcpy(e->words[e->count].text, tab);
        ++e->count;
    }
    if (ferror(file) || e->count < 1000)
        goto invalid;
    fclose(file);
    return 0;
invalid:
    fclose(file);
    ime_engine_close(e);
    return -1;
}
void ime_engine_close(struct ime_engine *e) {
    free(e->words);
    memset(e, 0, sizeof(*e));
}
void ime_engine_clear(struct ime_engine *e) {
    e->preedit[0] = 0;
    e->page = 0;
}
int ime_engine_letter(struct ime_engine *e, char c) {
    size_t n = strlen(e->preedit);
    if (c < 'a' || c > 'z' || n >= 48)
        return -1;
    e->preedit[n] = c;
    e->preedit[n + 1] = 0;
    e->page = 0;
    return 0;
}
void ime_engine_backspace(struct ime_engine *e) {
    size_t n = strlen(e->preedit);
    if (n)
        e->preedit[n - 1] = 0;
    e->page = 0;
}
size_t ime_engine_candidates(struct ime_engine *e, const char **out, size_t limit) {
    size_t lo = 0, hi = e->count;
    if (!*e->preedit)
        return 0;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(e->words[mid].key, e->preedit) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    size_t count = 0, skip = e->page * limit;
    while (lo < e->count && !strcmp(e->words[lo].key, e->preedit)) {
        if (skip)
            --skip;
        else {
            out[count++] = e->words[lo].text;
            if (count == limit)
                break;
        }
        ++lo;
    }
    return count;
}
