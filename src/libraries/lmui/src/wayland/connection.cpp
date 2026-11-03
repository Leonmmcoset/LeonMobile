#include "backend.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>
namespace lmui {
Application::Impl::~Impl() {
    for (auto &b : buffers) {
        if (b->buffer)
            wl_buffer_destroy(b->buffer);
        if (b->data != MAP_FAILED)
            munmap(b->data, b->size);
    }
    if (state)
        xkb_state_unref(state);
    if (keymap)
        xkb_keymap_unref(keymap);
    if (xkb)
        xkb_context_unref(xkb);
    if (touch)
        wl_touch_destroy(touch);
    if (pointer)
        wl_pointer_destroy(pointer);
    if (keyboard)
        wl_keyboard_destroy(keyboard);
    if (seat)
        wl_seat_destroy(seat);
    if (toplevel)
        xdg_toplevel_destroy(toplevel);
    if (xdg)
        xdg_surface_destroy(xdg);
    if (surface)
        wl_surface_destroy(surface);
    if (wm)
        xdg_wm_base_destroy(wm);
    if (shm)
        wl_shm_destroy(shm);
    if (compositor)
        wl_compositor_destroy(compositor);
    if (registry)
        wl_registry_destroy(registry);
    if (display)
        wl_display_disconnect(display);
}
void Application::Impl::global(void *data, wl_registry *reg, uint32_t name, const char *interface,
                               uint32_t version) {
    auto &s = *static_cast<Impl *>(data);
    if (!strcmp(interface, "wl_compositor"))
        s.compositor = static_cast<wl_compositor *>(
            wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    else if (!strcmp(interface, "wl_shm"))
        s.shm = static_cast<wl_shm *>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    else if (!strcmp(interface, "xdg_wm_base")) {
        s.wm = static_cast<xdg_wm_base *>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
        static const xdg_wm_base_listener events = {
            [](void *, xdg_wm_base *wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }};
        xdg_wm_base_add_listener(s.wm, &events, &s);
    } else if (!strcmp(interface, "wl_seat")) {
        s.seat = static_cast<wl_seat *>(
            wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        static const wl_seat_listener events = {capabilities,
                                                [](void *, wl_seat *, const char *) {}};
        wl_seat_add_listener(s.seat, &events, &s);
    }
}
int Application::Impl::run() {
    display = wl_display_connect(nullptr);
    if (!display)
        throw std::runtime_error("cannot connect to Wayland display");
    registry = wl_display_get_registry(display);
    static const wl_registry_listener globals = {global, [](void *, wl_registry *, uint32_t) {}};
    wl_registry_add_listener(registry, &globals, this);
    if (wl_display_roundtrip(display) < 0 || !compositor || !shm || !wm)
        throw std::runtime_error("Wayland compositor lacks required globals");
    surface = wl_compositor_create_surface(compositor);
    xdg = xdg_wm_base_get_xdg_surface(wm, surface);
    static const xdg_surface_listener surface_events = {
        [](void *d, xdg_surface *surface, uint32_t serial) {
            auto &s = *static_cast<Impl *>(d);
            xdg_surface_ack_configure(surface, serial);
            if (!s.configured)
                s.opened = std::chrono::steady_clock::now();
            s.configured = true;
            s.dirty = true;
        }};
    xdg_surface_add_listener(xdg, &surface_events, this);
    toplevel = xdg_surface_get_toplevel(xdg);
    static const xdg_toplevel_listener toplevel_events = {
        [](void *d, xdg_toplevel *, int32_t w, int32_t h, wl_array *) {
            auto &s = *static_cast<Impl *>(d);
            if (w > 0)
                s.w = std::clamp(w, 64, 4096);
            if (h > 0)
                s.h = std::clamp(h, 64, 4096);
            s.dirty = true;
        },
        [](void *d, xdg_toplevel *) { static_cast<Impl *>(d)->running = false; }
#ifdef XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION
        ,
        [](void *, xdg_toplevel *, int32_t, int32_t) {}
#endif
#ifdef XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION
        ,
        [](void *, xdg_toplevel *, wl_array *) {}
#endif
    };
    xdg_toplevel_add_listener(toplevel, &toplevel_events, this);
    xdg_toplevel_set_title(toplevel, title.c_str());
    xdg_toplevel_set_app_id(toplevel, id.c_str());
    wl_surface_commit(surface);
    auto last = std::chrono::steady_clock::now();
    while (running) {
        if (wl_display_dispatch_pending(display) < 0)
            break;
        auto now = std::chrono::steady_clock::now();
        if (now - last > std::chrono::milliseconds(200)) {
            if (app->tick)
                app->tick();
            last = now;
        }
        draw();
        if (wl_display_flush(display) < 0 && errno != EAGAIN)
            break;
        pollfd p{wl_display_get_fd(display), POLLIN, 0};
        int rc = poll(&p, 1, 16);
        if (rc < 0 && errno != EINTR)
            break;
        if (rc > 0) {
            if (p.revents & (POLLERR | POLLHUP))
                break;
            if ((p.revents & POLLIN) && wl_display_dispatch(display) < 0)
                break;
        }
    }
    return 0;
}
} // namespace lmui
