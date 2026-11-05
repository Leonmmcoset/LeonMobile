#include "document.h"
#include <unistd.h>
int main(int argc, char **argv) {
    struct documents state = {0};
    struct lm_daemon d = {.options = {"/run/leon/documents.sock", "/data/leon/system/documents.json",
        "/data/leon", "/usr/share/leon/apps", "/proc", "/sys", 0}, .data = &state,
        .handle = document_handle, .tick = document_expire};
    if (lm_options(argc, argv, &d.options))
        return 2;
    int result = lm_serve(&d);
    for (size_t i = 0; i < DOCUMENT_LIMIT; ++i)
        if (state.items[i].token[0] && state.items[i].fd >= 0)
            close(state.items[i].fd);
    return result;
}
