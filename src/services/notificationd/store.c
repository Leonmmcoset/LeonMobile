#include "notificationd.h"
#include <string.h>

void notification_store_add(struct notification_store *store, const char *app, const char *title,
                            size_t title_size) {
    size_t index = (store->start + store->count) % NOTIFICATION_CAPACITY;
    if (store->count == NOTIFICATION_CAPACITY)
        store->start = (store->start + 1) % NOTIFICATION_CAPACITY;
    else
        ++store->count;
    struct notification *item = &store->items[index];
    strcpy(item->app, app);
    memcpy(item->title, title, title_size);
    item->title[title_size] = '\0';
    item->title_size = title_size;
}

const struct notification *notification_store_at(const struct notification_store *store,
                                                 size_t index) {
    return &store->items[(store->start + index) % NOTIFICATION_CAPACITY];
}
