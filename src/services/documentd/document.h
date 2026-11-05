#ifndef LEON_DOCUMENTD_H
#define LEON_DOCUMENTD_H
#include "capability.h"
#define DOCUMENT_LIMIT 32
struct document {
    char token[33], parent[33], app[97], installation[33], name[241];
    uid_t uid;
    long long expires;
    int fd, state;
    size_t bytes;
};
struct documents { struct document items[DOCUMENT_LIMIT]; };
int document_snapshot(const char *base, const char *file, size_t *bytes);
void document_expire(struct lm_daemon *d);
lm_json *document_handle(struct lm_daemon *d, uid_t uid, const lm_json *r);
#endif
