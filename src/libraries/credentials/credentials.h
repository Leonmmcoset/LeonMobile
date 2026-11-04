#pragma once
#include <stddef.h>
#include <sys/types.h>
struct credential_hash {
    unsigned char salt[16], verifier[32];
};
void credential_clear(void *, size_t);
int credential_create(struct credential_hash *, const char *);
int credential_check(const struct credential_hash *, const char *);
int credential_random(char *, size_t bytes);
void credential_hex(const void *, size_t, char *);
int credential_unhex(const char *, void *, size_t);
