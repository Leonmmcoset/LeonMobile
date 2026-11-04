#ifndef LEON_PORTALD_H
#define LEON_PORTALD_H
#include "daemon.h"
struct request {
    unsigned token;
    uid_t uid;
    long long expires;
    char permission[32], app[97], selected[PATH_MAX];
    int file_request, completed;
};
struct portal {
    lm_json *grants;
    struct request pending[32];
};
int portald_run(int argc, char **argv);
int portal_granted(struct portal *, const lm_json *, const char *);
lm_json *portal_import(struct lm_daemon *, uid_t, const lm_json *);
lm_json *portal_files(struct lm_daemon *, uid_t, const lm_json *);
#endif
