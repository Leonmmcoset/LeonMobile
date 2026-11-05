#ifndef LEON_NOTIFICATIOND_H
#define LEON_NOTIFICATIOND_H

#include <stddef.h>
#include <sys/types.h>

#define NOTIFICATION_CAPACITY 64
#define NOTIFICATION_ID_LIMIT 96
#define NOTIFICATION_TITLE_LIMIT 160
#define NOTIFICATION_REQUEST_LIMIT 8191

struct notification_options {
    const char *root;
    const char *builtin;
    const char *socket_path;
    const char *auth_socket, *audio_socket;
    int development;
};

struct notification {
    char app[NOTIFICATION_ID_LIMIT + 1];
    char title[NOTIFICATION_TITLE_LIMIT + 1];
    size_t title_size;
};

struct notification_store {
    struct notification items[NOTIFICATION_CAPACITY];
    size_t start;
    size_t count;
    void *policy;
};

#ifdef __cplusplus
extern "C" {
#endif

void notification_store_add(struct notification_store *store, const char *app, const char *title,
                            size_t title_size);
const struct notification *notification_store_at(const struct notification_store *store,
                                                 size_t index);
/* The JSON/catalog adapter returns a malloc-owned, newline-terminated reply. */
char *notification_dispatch(struct notification_store *store,
                            const struct notification_options *options, uid_t caller,
                            const char *request, size_t size);
char *notification_error(const char *message);
int notification_serve(const struct notification_options *options);
int notification_policy_load(struct notification_store *, const struct notification_options *);
void notification_policy_free(struct notification_store *);
void notification_policy_tick(struct notification_store *, const struct notification_options *);

#ifdef __cplusplus
}
#endif
#endif
