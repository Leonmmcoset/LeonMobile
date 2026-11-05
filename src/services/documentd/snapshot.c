#include "document.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_BYTES (16 * 1024 * 1024)
int document_snapshot(const char *base, const char *file, size_t *bytes) {
    int source = lm_open_beneath(base, file, O_RDONLY | O_NONBLOCK);
    struct stat st;
    if (source < 0)
        return -1;
    if (fstat(source, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > MAX_BYTES) {
        close(source);
        return -1;
    }
    int fd = memfd_create("leon-selected-document", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    char buffer[16384];
    size_t used = 0;
    int failed = fd < 0;
    while (!failed) {
        ssize_t n = read(source, buffer, sizeof(buffer));
        if (!n)
            break;
        if (n < 0 || used + (size_t)n > MAX_BYTES || write(fd, buffer, (size_t)n) != n) {
            failed = 1;
            break;
        }
        used += (size_t)n;
    }
    close(source);
    if (fd >= 0 && (failed || fcntl(fd, F_ADD_SEALS,
        F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL))) {
        close(fd);
        return -1;
    }
    *bytes = used;
    return fd;
}
