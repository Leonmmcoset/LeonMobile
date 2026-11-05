#ifndef LEON_CAPABILITY_H
#define LEON_CAPABILITY_H
#include "daemon.h"
int lm_token(char out[33]);
int lm_hex_decode(const char *hex, void *data, size_t capacity);
void lm_hex_encode(const void *data, size_t size, char *hex);
int lm_identity_matches(struct lm_daemon *d, uid_t uid, const char *id, const char *instance);
int lm_declares(const lm_json *app, const char *permission);
int lm_open_beneath(const char *base, const char *relative, int flags);
lm_json *lm_shared_files(struct lm_daemon *d, uid_t uid, const lm_json *r);
#endif
