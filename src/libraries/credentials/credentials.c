#include "credentials.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
void credential_clear(void *p, size_t size) { OPENSSL_cleanse(p, size); }
static int derive(const char *secret, const unsigned char *salt, unsigned char *out) {
    return EVP_PBE_scrypt(secret, strlen(secret), salt, 16, 32768, 8, 1, 64 * 1024 * 1024, out,
                          32) == 1
               ? 0
               : -1;
}
int credential_create(struct credential_hash *hash, const char *secret) {
    return RAND_bytes(hash->salt, sizeof(hash->salt)) == 1
               ? derive(secret, hash->salt, hash->verifier)
               : -1;
}
int credential_check(const struct credential_hash *hash, const char *secret) {
    unsigned char out[32];
    int result = derive(secret, hash->salt, out);
    if (!result)
        result = CRYPTO_memcmp(out, hash->verifier, sizeof(out)) ? 1 : 0;
    credential_clear(out, sizeof(out));
    return result;
}
void credential_hex(const void *data, size_t size, char *out) {
    const unsigned char *p = data;
    for (size_t i = 0; i < size; ++i)
        snprintf(out + i * 2, 3, "%02x", p[i]);
}
int credential_unhex(const char *text, void *data, size_t size) {
    unsigned char *out = data;
    if (strlen(text) != size * 2)
        return -1;
    for (size_t i = 0; i < size; ++i) {
        unsigned value = 0;
        for (size_t j = 0; j < 2; ++j) {
            unsigned char c = text[i * 2 + j];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return -1;
            value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        out[i] = (unsigned char)value;
    }
    return 0;
}
int credential_random(char *out, size_t size) {
    unsigned char bytes[32];
    if (!size || size > sizeof(bytes) || RAND_bytes(bytes, (int)size) != 1)
        return -1;
    credential_hex(bytes, size, out);
    credential_clear(bytes, sizeof(bytes));
    return 0;
}
