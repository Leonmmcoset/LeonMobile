#include "recovery.h"
#include <errno.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int recovery_hash(int fd, uint64_t size, char hash[65]) {
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    unsigned char *buffer = malloc(1024 * 1024), digest[32];
    unsigned length = 0;
    int good = context && buffer && EVP_DigestInit_ex(context, EVP_sha256(), NULL) == 1;
    for (uint64_t offset = 0; good && offset < size;) {
        size_t count = size - offset > 1024 * 1024 ? 1024 * 1024 : (size_t)(size - offset);
        ssize_t n = pread(fd, buffer, count, (off_t)offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { good = 0; break; }
        good = EVP_DigestUpdate(context, buffer, (size_t)n) == 1;
        offset += (uint64_t)n;
    }
    good = good && EVP_DigestFinal_ex(context, digest, &length) == 1 && length == 32;
    EVP_MD_CTX_free(context); free(buffer);
    if (!good) return -1;
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { hash[2 * i] = hex[digest[i] >> 4]; hash[2 * i + 1] = hex[digest[i] & 15]; }
    hash[64] = 0; return 0;
}
int recovery_verify(int fd, uint64_t size, const char *hash) {
    unsigned char super[1024];
    if (size < 4096 || size > 1024ULL * 1024 * 1024 || strlen(hash) != 64 ||
        pread(fd, super, sizeof(super), 1024) != sizeof(super) || super[56] != 0x53 || super[57] != 0xef)
        return -1;
    for (int i = 0; i < 64; ++i)
        if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f'))) return -1;
    char actual[65];
    return recovery_hash(fd, size, actual) || strcmp(actual, hash) ? -1 : 0;
}
