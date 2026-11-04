/* SPDX-License-Identifier: CC0-1.0
 * Based on wlroots 0.20 tinywl; mobile policy additions by LeonMobile. */
#pragma once
#include "auth.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* For brevity's sake, struct members are annotated where they are used. */
struct leon_server {
    struct leon_toplevel *shell, *ime;
    struct wlr_input_method_v2 *input_method;
    struct wlr_text_input_v3 *active_text;
    struct wl_list text_inputs;
    struct wl_listener new_text_input, new_input_method, text_focus, method_commit, method_destroy;
    struct wlr_keyboard fallback_keyboard;
    struct wl_listener touch_down, touch_up, touch_motion;
    bool session_locked, session_modal;
    bool auth_live, auth_consuming, lock_ready;
    int auth_fd;
    struct wl_event_source *auth_source, *auth_timer;
    struct wlr_scene_rect *lock_cover;
    char auth_epoch[33], auth_buffer[LEON_AUTH_LIMIT];
    size_t auth_used;
    long long auth_received, auth_sent;
    long long last_activity;
    int wm_fd;
    struct wl_event_source *wm_source;
    int width, height;
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat *seat;
    double pointer_last_x, pointer_last_y;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener pointer_focus_change;
    struct wl_listener request_set_selection;
    struct wl_list keyboards;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;
};

struct leon_output {
    struct wl_list link;
    struct leon_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct leon_toplevel {
    bool is_shell, is_ime;
    struct wl_list link;
    struct leon_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

struct leon_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct leon_keyboard {
    struct wl_list link;
    struct leon_server *server;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

/* Internal subsystem interfaces; never installed as application SDK headers. */
void focus_toplevel(struct leon_toplevel *toplevel);
struct leon_toplevel *desktop_toplevel_at(struct leon_server *server, double lx, double ly,
                                          struct wlr_surface **surface, double *sx, double *sy);
void process_cursor_motion(struct leon_server *server, uint32_t time);
void leon_new_keyboard(struct leon_server *server, struct wlr_input_device *device);
bool leon_input_init(struct leon_server *server);
void leon_input_detach(struct leon_server *server);
void leon_input_destroy(struct leon_server *server);
void leon_pointer_init(struct leon_server *server);
void leon_pointer_finish(struct leon_server *server);
void leon_touch_init(struct leon_server *server);
void leon_touch_finish(struct leon_server *server);
void leon_outputs_init(struct leon_server *server);
void leon_outputs_finish(struct leon_server *server);
void leon_windows_init(struct leon_server *server);
void leon_windows_finish(struct leon_server *server);
void leon_text_init(struct leon_server *server);
void leon_text_finish(struct leon_server *server);
void leon_text_geometry(struct leon_server *server);
void leon_activity(struct leon_server *server);
bool leon_control_init(struct leon_server *server);
void leon_control_finish(struct leon_server *server);

bool leon_session_init(struct leon_server *server);
void leon_session_finish(struct leon_server *server);
void leon_session_lock(struct leon_server *server);
void leon_session_visibility(struct leon_server *server);
void leon_session_ready(struct leon_server *server, const char *epoch);
void leon_session_unlock(struct leon_server *server, const char *token);
bool leon_session_peer(pid_t pid, uid_t uid, const char *role);
