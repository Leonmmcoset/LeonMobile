#include "backend.h"
#include "input-method-client-protocol.h"
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
struct ime_backend {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct zwp_input_method_manager_v2 *manager;
    struct zwp_input_method_v2 *method;
    uint32_t serial, purpose, before;
    unsigned revision;
    int active, pending_active, unavailable;
};
static void activate(void *data, struct zwp_input_method_v2 *im) {
    (void)im;
    struct ime_backend *b = data;
    b->pending_active = 1;
    b->purpose = 0;
    b->before = 1;
}
static void deactivate(void *data, struct zwp_input_method_v2 *im) {
    (void)im;
    ((struct ime_backend *)data)->pending_active = 0;
}
static void surrounding(void *data, struct zwp_input_method_v2 *im, const char *text,
                        uint32_t cursor, uint32_t anchor) {
    (void)im;
    (void)anchor;
    struct ime_backend *b = data;
    size_t n = strlen(text);
    b->before = 0;
    if (cursor > 0 && cursor <= n) {
        size_t start = cursor - 1;
        while (start && ((unsigned char)text[start] & 0xc0) == 0x80)
            --start;
        b->before = cursor - (uint32_t)start;
    }
}
static void cause(void *data, struct zwp_input_method_v2 *im, uint32_t value) {
    (void)data;
    (void)im;
    (void)value;
}
static void content(void *data, struct zwp_input_method_v2 *im, uint32_t hint, uint32_t purpose) {
    (void)im;
    (void)hint;
    ((struct ime_backend *)data)->purpose = purpose;
}
static void done(void *data, struct zwp_input_method_v2 *im) {
    (void)im;
    struct ime_backend *b = data;
    ++b->serial;
    if (b->active != b->pending_active)
        ++b->revision;
    b->active = b->pending_active;
}
static void unavailable(void *data, struct zwp_input_method_v2 *im) {
    (void)im;
    ((struct ime_backend *)data)->unavailable = 1;
}
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version) {
    (void)version;
    struct ime_backend *b = data;
    if (!strcmp(interface, "wl_seat"))
        b->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    if (!strcmp(interface, "zwp_input_method_manager_v2"))
        b->manager = wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1);
}
static void removed(void *data, struct wl_registry *r, uint32_t name) {
    (void)data;
    (void)r;
    (void)name;
}
struct ime_backend *ime_backend_open(void) {
    struct ime_backend *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->display = wl_display_connect(NULL);
    if (!b->display)
        goto error;
    b->registry = wl_display_get_registry(b->display);
    static const struct wl_registry_listener registry = {global, removed};
    wl_registry_add_listener(b->registry, &registry, b);
    if (wl_display_roundtrip(b->display) < 0 || !b->seat || !b->manager)
        goto error;
    b->method = zwp_input_method_manager_v2_get_input_method(b->manager, b->seat);
    static const struct zwp_input_method_v2_listener events = {
        activate, deactivate, surrounding, cause, content, done, unavailable};
    zwp_input_method_v2_add_listener(b->method, &events, b);
    if (wl_display_roundtrip(b->display) < 0 || b->unavailable)
        goto error;
    return b;
error:
    ime_backend_close(b);
    return NULL;
}
void ime_backend_close(struct ime_backend *b) {
    if (!b)
        return;
    if (b->method)
        zwp_input_method_v2_destroy(b->method);
    if (b->manager)
        zwp_input_method_manager_v2_destroy(b->manager);
    if (b->seat)
        wl_seat_destroy(b->seat);
    if (b->registry)
        wl_registry_destroy(b->registry);
    if (b->display)
        wl_display_disconnect(b->display);
    free(b);
}
int ime_backend_fd(struct ime_backend *b) { return wl_display_get_fd(b->display); }
int ime_backend_poll(struct ime_backend *b) {
    if (wl_display_dispatch_pending(b->display) < 0)
        return -1;
    if (wl_display_prepare_read(b->display))
        return -1;
    wl_display_flush(b->display);
    struct pollfd fd = {wl_display_get_fd(b->display), POLLIN, 0};
    if (poll(&fd, 1, 0) > 0) {
        if (wl_display_read_events(b->display) < 0)
            return -1;
        if (wl_display_dispatch_pending(b->display) < 0)
            return -1;
    } else
        wl_display_cancel_read(b->display);
    return b->unavailable ? -1 : 0;
}
int ime_backend_active(struct ime_backend *b) { return b->active; }
int ime_backend_private(struct ime_backend *b) { return b->purpose == 8 || b->purpose == 9; }
unsigned ime_backend_revision(struct ime_backend *b) { return b->revision; }
void ime_backend_send(struct ime_backend *b, const char *commit, const char *preedit,
                      int backspace) {
    if (!b->active)
        return;
    zwp_input_method_v2_set_preedit_string(b->method, preedit, (int32_t)strlen(preedit),
                                           (int32_t)strlen(preedit));
    if (commit && *commit)
        zwp_input_method_v2_commit_string(b->method, commit);
    if (backspace)
        zwp_input_method_v2_delete_surrounding_text(b->method, b->before ? b->before : 1, 0);
    zwp_input_method_v2_commit(b->method, b->serial);
    wl_display_flush(b->display);
}
