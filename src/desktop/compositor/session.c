/* SPDX-License-Identifier: Apache-2.0 */
#include "server.h"
#include <wlr/types/wlr_data_device.h>

static long long now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

bool leon_session_peer(pid_t pid, uid_t uid, const char *role) {
    /* Peer /proc/exe can be unreadable from UID 1000 for nondumpable processes.
     * The root authentication service performs the identity check. */
    return auth_verify_peer(LEON_AUTH_SOCKET, pid, uid, role);
}

void leon_session_visibility(struct leon_server *s) {
    if (!s->lock_cover)
        return;
    struct leon_toplevel *view;
    wl_list_for_each(view, &s->toplevels, link) {
        bool visible = !s->session_locked || (view->is_shell && s->lock_ready && s->auth_live);
        if (view->is_ime)
            visible = s->active_text && s->input_method &&
                      (!s->session_locked || (s->lock_ready && s->auth_live));
        wlr_scene_node_set_enabled(&view->scene_tree->node, visible);
    }
    wlr_scene_rect_set_size(s->lock_cover, s->width, s->height);
    wlr_scene_node_set_enabled(&s->lock_cover->node, s->session_locked);
    if (s->session_locked) {
        wlr_scene_node_raise_to_top(&s->lock_cover->node);
        if (s->shell && s->lock_ready && s->auth_live)
            wlr_scene_node_raise_to_top(&s->shell->scene_tree->node);
        if (s->ime && s->active_text && s->lock_ready && s->auth_live)
            wlr_scene_node_raise_to_top(&s->ime->scene_tree->node);
    }
}

static void cover(struct leon_server *s) {
    s->session_locked = true;
    s->session_modal = false;
    s->lock_ready = false;
    wlr_seat_keyboard_end_grab(s->seat);
    wlr_seat_pointer_end_grab(s->seat);
    wlr_seat_touch_end_grab(s->seat);
    wlr_seat_keyboard_clear_focus(s->seat);
    wlr_seat_pointer_clear_focus(s->seat);
    /* Physical releases after locking must not retain an app's implicit grab. */
    memset(s->seat->pointer_state.buttons, 0, sizeof(s->seat->pointer_state.buttons));
    s->seat->pointer_state.button_count = 0;
    wlr_seat_set_selection(s->seat, NULL, 0);
    leon_text_geometry(s);
    leon_session_visibility(s);
}

static void disconnect_auth(struct leon_server *s) {
    if (s->auth_source)
        wl_event_source_remove(s->auth_source);
    s->auth_source = NULL;
    if (s->auth_fd >= 0)
        close(s->auth_fd);
    s->auth_fd = -1;
    s->auth_live = s->auth_consuming = false;
    s->auth_used = 0;
    s->auth_epoch[0] = 0;
    cover(s);
}

void leon_session_lock(struct leon_server *s) {
    /* Reattaching the monitor atomically rotates authd's lock epoch. */
    disconnect_auth(s);
}

static int auth_event(int fd, uint32_t mask, void *data) {
    struct leon_server *s = data;
    if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
        disconnect_auth(s);
        return 0;
    }
    for (;;) {
        ssize_t n = recv(fd, s->auth_buffer + s->auth_used,
                         sizeof(s->auth_buffer) - 1 - s->auth_used, MSG_DONTWAIT);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            break;
        if (n <= 0 || memchr(s->auth_buffer + s->auth_used, 0, (size_t)n)) {
            disconnect_auth(s);
            return 0;
        }
        s->auth_used += (size_t)n;
        char *end;
        while ((end = memchr(s->auth_buffer, '\n', s->auth_used))) {
            size_t length = (size_t)(end - s->auth_buffer) + 1;
            *end = 0;
            struct auth_state state;
            if (auth_decode(s->auth_buffer, &state) || !state.ok || strlen(state.epoch) != 32) {
                disconnect_auth(s);
                return 0;
            }
            s->auth_received = now_ms();
            s->auth_live = true;
            if (strcmp(s->auth_epoch, state.epoch)) {
                strcpy(s->auth_epoch, state.epoch);
                cover(s);
            }
            if (state.locked) {
                if (!s->session_locked)
                    cover(s);
            } else if (s->session_locked) {
                /* Only a consumed grant on this live connection can reveal apps. */
                if (!s->auth_consuming || !s->lock_ready || !s->shell) {
                    disconnect_auth(s);
                    return 0;
                }
                s->auth_consuming = false;
                s->session_locked = false;
                leon_session_visibility(s);
                focus_toplevel(s->shell);
            }
            s->auth_used -= length;
            memmove(s->auth_buffer, s->auth_buffer + length, s->auth_used);
        }
        if (s->auth_used == sizeof(s->auth_buffer) - 1) {
            disconnect_auth(s);
            return 0;
        }
    }
    return 0;
}

static int auth_tick(void *data) {
    struct leon_server *s = data;
    /* Hidden scene nodes do not receive output frame callbacks. Wake a shell
     * finishing an old animation so it can commit its opaque lock page. */
    if (s->session_locked && !s->lock_ready && s->shell) {
        struct timespec frame;
        clock_gettime(CLOCK_MONOTONIC, &frame);
        wlr_surface_send_frame_done(s->shell->xdg_toplevel->base->surface, &frame);
    }
    long long now = now_ms();
    if (s->auth_fd >= 0 &&
        (now - s->auth_received > 1250 || (s->auth_consuming && now - s->auth_sent > 1500)))
        disconnect_auth(s);
    if (s->auth_fd < 0) {
        s->auth_fd = auth_watch(LEON_AUTH_SOCKET);
        if (s->auth_fd >= 0) {
            s->auth_received = now;
            s->auth_source = wl_event_loop_add_fd(wl_display_get_event_loop(s->wl_display),
                                                  s->auth_fd, WL_EVENT_READABLE, auth_event, s);
            if (!s->auth_source)
                disconnect_auth(s);
        }
    }
    wl_event_source_timer_update(s->auth_timer, 100);
    return 0;
}

void leon_session_ready(struct leon_server *s, const char *epoch) {
    if (!s->session_locked || !s->auth_live || !s->shell ||
        !s->shell->xdg_toplevel->base->surface->mapped || strcmp(epoch, s->auth_epoch))
        return;
    s->lock_ready = true;
    leon_session_visibility(s);
    focus_toplevel(s->shell);
}

void leon_session_unlock(struct leon_server *s, const char *token) {
    if (!s->session_locked || !s->auth_live || !s->lock_ready || s->auth_consuming ||
        strlen(token) != 64)
        return;
    for (size_t i = 0; i < 64; ++i)
        if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f')))
            return;
    char request[160];
    int count = snprintf(request, sizeof(request),
                         "{\"op\":\"consume\",\"purpose\":\"unlock\",\"token\":\"%s\"}\n", token);
    if (send(s->auth_fd, request, (size_t)count, MSG_DONTWAIT | MSG_NOSIGNAL) != count) {
        disconnect_auth(s);
        return;
    }
    s->auth_consuming = true;
    s->auth_sent = now_ms();
}

bool leon_session_init(struct leon_server *s) {
    s->auth_fd = -1;
    const float color[] = {.06f, .05f, .08f, 1.f};
    s->lock_cover = wlr_scene_rect_create(&s->scene->tree, s->width, s->height, color);
    if (!s->lock_cover)
        return false;
    s->auth_timer = wl_event_loop_add_timer(wl_display_get_event_loop(s->wl_display), auth_tick, s);
    if (!s->auth_timer)
        return false;
    cover(s);
    wl_event_source_timer_update(s->auth_timer, 1);
    return true;
}

void leon_session_finish(struct leon_server *s) {
    if (s->auth_timer)
        wl_event_source_remove(s->auth_timer);
    s->auth_timer = NULL;
    disconnect_auth(s);
}
