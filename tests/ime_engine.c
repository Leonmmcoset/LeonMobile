#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    char fixture[] = "/tmp/leon-ime-test-XXXXXX";
    const char *path = argc == 2 ? argv[1] : fixture;
    if (argc != 2) {
        int fd = mkstemp(fixture);
        assert(fd >= 0);
        FILE *f = fdopen(fd, "w");
        assert(f);
        fputs("ni\t你\n", f);
        for (int i = 0; i < 1000; ++i)
            fputs("ni\t倪\n", f);
        fputs("nihao\t你好\n", f);
        assert(!fclose(f));
    }
    struct ime_engine e = {0};
    assert(!ime_engine_open(&e, path));
    assert(e.count >= 1000);
    const char *out[6];
    for (const char *p = "nihao"; *p; ++p)
        assert(!ime_engine_letter(&e, *p));
    assert(ime_engine_candidates(&e, out, 6) > 0);
    assert(!strcmp(out[0], "你好"));
    ime_engine_clear(&e);
    ime_engine_letter(&e, 'n');
    ime_engine_letter(&e, 'i');
    assert(ime_engine_candidates(&e, out, 6) == 6);
    assert(!strcmp(out[0], "你"));
    e.page = 1;
    assert(ime_engine_candidates(&e, out, 6) == 6);
    assert(strcmp(out[0], "你"));
    ime_engine_backspace(&e);
    assert(!strcmp(e.preedit, "n"));
    assert(e.page == 0);
    ime_engine_clear(&e);
    for (int i = 0; i < 48; ++i)
        assert(!ime_engine_letter(&e, 'x'));
    assert(ime_engine_letter(&e, 'x') < 0);
    assert(!ime_engine_candidates(&e, out, 6));
    assert(ime_engine_letter(&e, '/') < 0);
    ime_engine_close(&e);
    if (argc != 2)
        unlink(fixture);
    return 0;
}
