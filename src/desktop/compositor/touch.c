/* SPDX-License-Identifier: CC0-1.0
 * Based on wlroots 0.20 tinywl; mobile policy additions by LeonMobile. */
#include "server.h"
static void touch_down(struct wl_listener *listener, void *data) {
    struct leon_server *server = wl_container_of(listener, server, touch_down);
    struct wlr_touch_down_event *event = data;
    if (event->touch_id != 0)
        return;
    wlr_cursor_warp_absolute(server->cursor, &event->touch->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct leon_toplevel *view =
        desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    focus_toplevel(view);
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
                                   WL_POINTER_BUTTON_STATE_PRESSED);
    wlr_seat_pointer_notify_frame(server->seat);
}
static void touch_motion(struct wl_listener *listener, void *data) {
    struct leon_server *server = wl_container_of(listener, server, touch_motion);
    struct wlr_touch_motion_event *event = data;
    if (event->touch_id != 0)
        return;
    wlr_cursor_warp_absolute(server->cursor, &event->touch->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
    wlr_seat_pointer_notify_frame(server->seat);
}
static void touch_up(struct wl_listener *listener, void *data) {
    struct leon_server *server = wl_container_of(listener, server, touch_up);
    struct wlr_touch_up_event *event = data;
    if (event->touch_id != 0)
        return;
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
                                   WL_POINTER_BUTTON_STATE_RELEASED);
    process_cursor_motion(server, event->time_msec);
    wlr_seat_pointer_notify_frame(server->seat);
}
void leon_touch_init(struct leon_server *server) {
    server->touch_down.notify = touch_down;
    wl_signal_add(&server->cursor->events.touch_down, &server->touch_down);
    server->touch_motion.notify = touch_motion;
    wl_signal_add(&server->cursor->events.touch_motion, &server->touch_motion);
    server->touch_up.notify = touch_up;
    wl_signal_add(&server->cursor->events.touch_up, &server->touch_up);
}
void leon_touch_finish(struct leon_server *server) {
    wl_list_remove(&server->touch_down.link);
    wl_list_remove(&server->touch_motion.link);
    wl_list_remove(&server->touch_up.link);
}
