#include "server.h"
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_text_input_v3.h>
struct leon_text_input {
    struct wl_list link;
    struct leon_server *server;
    struct wlr_text_input_v3 *input;
    struct wl_listener enable, disable, commit, destroy;
};
static void geometry(struct leon_server *s) {
    bool visible = s->active_text && s->input_method &&
                   (!s->session_locked || (s->lock_ready && s->auth_live && s->shell &&
                                           s->seat->keyboard_state.focused_surface ==
                                               s->shell->xdg_toplevel->base->surface));
    if (s->ime) {
        wlr_scene_node_set_enabled(&s->ime->scene_tree->node, visible);
        if (visible)
            wlr_scene_node_raise_to_top(&s->ime->scene_tree->node);
    }
    if (s->session_locked)
        return;
    struct leon_toplevel *v;
    wl_list_for_each(v, &s->toplevels, link) {
        if (v->is_shell || v->is_ime)
            continue;
        wlr_xdg_toplevel_set_size(v->xdg_toplevel, s->width, s->height - 106 - (visible ? 300 : 0));
    }
}
void leon_text_geometry(struct leon_server *s) { geometry(s); }
static void update(struct leon_server *s, bool changed) {
    struct wlr_text_input_v3 *active = NULL;
    struct leon_text_input *t;
    struct wlr_surface *focus = s->seat->keyboard_state.focused_surface;
    wl_list_for_each(t, &s->text_inputs, link) {
        struct wlr_text_input_v3 *ti = t->input;
        bool match = focus && wl_resource_get_client(ti->resource) ==
                                  wl_resource_get_client(focus->resource);
        if (ti->focused_surface && (!match || ti->focused_surface != focus))
            wlr_text_input_v3_send_leave(ti);
        if (match && !ti->focused_surface)
            wlr_text_input_v3_send_enter(ti, focus);
        if (match && ti->current_enabled)
            active = ti;
    }
    if (active != s->active_text) {
        if (s->input_method && s->active_text) {
            wlr_input_method_v2_send_deactivate(s->input_method);
            wlr_input_method_v2_send_done(s->input_method);
        }
        s->active_text = active;
        if (s->input_method && active)
            wlr_input_method_v2_send_activate(s->input_method);
        changed = true;
    }
    if (s->input_method && active && changed) {
        struct wlr_text_input_v3_state *state = &active->current;
        if (active->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
            wlr_input_method_v2_send_surrounding_text(
                s->input_method, state->surrounding.text ? state->surrounding.text : "",
                state->surrounding.cursor, state->surrounding.anchor);
        wlr_input_method_v2_send_content_type(s->input_method, state->content_type.hint,
                                              state->content_type.purpose);
        wlr_input_method_v2_send_text_change_cause(s->input_method, state->text_change_cause);
        wlr_input_method_v2_send_done(s->input_method);
    }
    geometry(s);
}
static void focus_change(struct wl_listener *listener, void *data) {
    (void)data;
    struct leon_server *s = wl_container_of(listener, s, text_focus);
    update(s, false);
}
static void text_enable(struct wl_listener *listener, void *data) {
    (void)data;
    struct leon_text_input *t = wl_container_of(listener, t, enable);
    update(t->server, true);
}
static void text_disable(struct wl_listener *listener, void *data) {
    (void)data;
    struct leon_text_input *t = wl_container_of(listener, t, disable);
    update(t->server, true);
}
static void text_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct leon_text_input *t = wl_container_of(listener, t, commit);
    update(t->server, true);
}
static void text_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct leon_text_input *t = wl_container_of(listener, t, destroy);
    wl_list_remove(&t->link);
    wl_list_remove(&t->enable.link);
    wl_list_remove(&t->disable.link);
    wl_list_remove(&t->commit.link);
    wl_list_remove(&t->destroy.link);
    if (t->server->active_text == t->input) {
        if (t->server->input_method) {
            wlr_input_method_v2_send_deactivate(t->server->input_method);
            wlr_input_method_v2_send_done(t->server->input_method);
        }
        t->server->active_text = NULL;
    }
    update(t->server, false);
    free(t);
}
static void new_text(struct wl_listener *listener, void *data) {
    struct leon_server *s = wl_container_of(listener, s, new_text_input);
    struct wlr_text_input_v3 *ti = data;
    if (ti->seat != s->seat)
        return;
    struct leon_text_input *t = calloc(1, sizeof(*t));
    if (!t)
        return;
    t->server = s;
    t->input = ti;
    t->enable.notify = text_enable;
    wl_signal_add(&ti->events.enable, &t->enable);
    t->disable.notify = text_disable;
    wl_signal_add(&ti->events.disable, &t->disable);
    t->commit.notify = text_commit;
    wl_signal_add(&ti->events.commit, &t->commit);
    t->destroy.notify = text_destroy;
    wl_signal_add(&ti->events.destroy, &t->destroy);
    wl_list_insert(&s->text_inputs, &t->link);
    update(s, false);
}
static void method_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct leon_server *s = wl_container_of(listener, s, method_commit);
    if (!s->active_text || !s->input_method)
        return;
    struct wlr_input_method_v2_state *state = &s->input_method->current;
    struct wlr_text_input_v3 *ti = s->active_text;
    wlr_text_input_v3_send_preedit_string(ti, state->preedit.text ? state->preedit.text : "",
                                          state->preedit.cursor_begin, state->preedit.cursor_end);
    if (state->delete.before_length || state->delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(ti, state->delete.before_length,
                                                       state->delete.after_length);
    if (state->commit_text)
        wlr_text_input_v3_send_commit_string(ti, state->commit_text);
    wlr_text_input_v3_send_done(ti);
}
static void method_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct leon_server *s = wl_container_of(listener, s, method_destroy);
    wl_list_remove(&s->method_commit.link);
    wl_list_remove(&s->method_destroy.link);
    s->input_method = NULL;
    if (s->active_text) {
        wlr_text_input_v3_send_preedit_string(s->active_text, "", 0, 0);
        wlr_text_input_v3_send_done(s->active_text);
    }
    geometry(s);
}
static void new_method(struct wl_listener *listener, void *data) {
    struct leon_server *s = wl_container_of(listener, s, new_input_method);
    struct wlr_input_method_v2 *im = data;
    uid_t uid;
    pid_t pid;
    wl_client_get_credentials(wl_resource_get_client(im->resource), &pid, &uid, NULL);
    if (!leon_session_peer(pid, uid, "ime") || s->input_method || im->seat != s->seat) {
        wlr_input_method_v2_send_unavailable(im);
        return;
    }
    s->input_method = im;
    s->method_commit.notify = method_commit;
    wl_signal_add(&im->events.commit, &s->method_commit);
    s->method_destroy.notify = method_destroy;
    wl_signal_add(&im->events.destroy, &s->method_destroy);
    if (s->active_text)
        wlr_input_method_v2_send_activate(im);
    update(s, true);
}
void leon_text_init(struct leon_server *s) {
    wl_list_init(&s->text_inputs);
    struct wlr_text_input_manager_v3 *tm = wlr_text_input_manager_v3_create(s->wl_display);
    struct wlr_input_method_manager_v2 *im = wlr_input_method_manager_v2_create(s->wl_display);
    s->new_text_input.notify = new_text;
    wl_signal_add(&tm->events.new_text_input, &s->new_text_input);
    s->new_input_method.notify = new_method;
    wl_signal_add(&im->events.new_input_method, &s->new_input_method);
    s->text_focus.notify = focus_change;
    wl_signal_add(&s->seat->keyboard_state.events.focus_change, &s->text_focus);
}
void leon_text_finish(struct leon_server *s) {
    wl_list_remove(&s->new_text_input.link);
    wl_list_remove(&s->new_input_method.link);
    wl_list_remove(&s->text_focus.link);
}
