#include "xdg-shell-client-protocol.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <lmui/lmui.hpp>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace lmui {
struct Application::Impl {
    Application *app;
    std::string id, title;
    wl_display *display{};
    wl_registry *registry{};
    wl_compositor *compositor{};
    wl_shm *shm{};
    xdg_wm_base *wm{};
    wl_seat *seat{};
    wl_pointer *pointer{};
    wl_keyboard *keyboard{};
    wl_touch *touch{};
    wl_surface *surface{};
    xdg_surface *xdg{};
    xdg_toplevel *toplevel{};
    xkb_context *xkb{};
    xkb_keymap *keymap{};
    xkb_state *state{};
    int w{480}, h{960};
    double px{}, py{};
    bool running{true}, configured{}, dirty{true};
    struct Buffer {
        Impl *owner;
        wl_buffer *buffer{};
        void *data{MAP_FAILED};
        size_t size{};
        int w{}, h{};
        bool busy{};
    };
    std::vector<std::unique_ptr<Buffer>> buffers;
    explicit Impl(Application *a, std::string i, std::string t)
        : app(a), id(std::move(i)), title(std::move(t)) {}
    ~Impl() {
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
    void notify_pointer(bool down) {
        if (app->pointer)
            app->pointer(px, py, down);
        dirty = true;
    }
    static void global(void *data, wl_registry *reg, uint32_t name, const char *interface,
                       uint32_t version) {
        auto &s = *static_cast<Impl *>(data);
        if (!strcmp(interface, "wl_compositor"))
            s.compositor = static_cast<wl_compositor *>(
                wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
        else if (!strcmp(interface, "wl_shm"))
            s.shm = static_cast<wl_shm *>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
        else if (!strcmp(interface, "xdg_wm_base")) {
            s.wm =
                static_cast<xdg_wm_base *>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
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
    static void capabilities(void *data, wl_seat *seat, uint32_t caps) {
        auto &s = *static_cast<Impl *>(data);
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s.pointer) {
            s.pointer = wl_seat_get_pointer(seat);
            static const wl_pointer_listener events = {
                [](void *d, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t x, wl_fixed_t y) {
                    auto &s = *static_cast<Impl *>(d);
                    s.px = wl_fixed_to_double(x);
                    s.py = wl_fixed_to_double(y);
                },
                [](void *, wl_pointer *, uint32_t, wl_surface *) {},
                [](void *d, wl_pointer *, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                    auto &s = *static_cast<Impl *>(d);
                    s.px = wl_fixed_to_double(x);
                    s.py = wl_fixed_to_double(y);
                },
                [](void *d, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t state) {
                    static_cast<Impl *>(d)->notify_pointer(state ==
                                                           WL_POINTER_BUTTON_STATE_PRESSED);
                },
                [](void *d, wl_pointer *, uint32_t, uint32_t axis, wl_fixed_t value) {
                    auto &s = *static_cast<Impl *>(d);
                    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL && s.app->scroll)
                        s.app->scroll(wl_fixed_to_double(value));
                    s.dirty = true;
                },
                [](void *, wl_pointer *) {},
                [](void *, wl_pointer *, uint32_t) {},
                [](void *, wl_pointer *, uint32_t, uint32_t) {},
                [](void *, wl_pointer *, uint32_t, int32_t) {}};
            wl_pointer_add_listener(s.pointer, &events, &s);
        }
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s.keyboard) {
            s.keyboard = wl_seat_get_keyboard(seat);
            s.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            static const wl_keyboard_listener events = {
                [](void *d, wl_keyboard *, uint32_t format, int fd, uint32_t size) {
                    auto &s = *static_cast<Impl *>(d);
                    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size) {
                        void *map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
                        if (map != MAP_FAILED) {
                            auto *km = xkb_keymap_new_from_string(s.xkb, static_cast<char *>(map),
                                                                  XKB_KEYMAP_FORMAT_TEXT_V1,
                                                                  XKB_KEYMAP_COMPILE_NO_FLAGS);
                            if (km) {
                                if (s.state)
                                    xkb_state_unref(s.state);
                                if (s.keymap)
                                    xkb_keymap_unref(s.keymap);
                                s.keymap = km;
                                s.state = xkb_state_new(km);
                            }
                            munmap(map, size);
                        }
                    }
                    close(fd);
                },
                [](void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {},
                [](void *, wl_keyboard *, uint32_t, wl_surface *) {},
                [](void *d, wl_keyboard *, uint32_t, uint32_t, uint32_t key, uint32_t state) {
                    auto &s = *static_cast<Impl *>(d);
                    if (!s.state)
                        return;
                    char text[128]{};
                    xkb_state_key_get_utf8(s.state, key + 8, text, sizeof(text));
                    KeyEvent e{xkb_state_key_get_one_sym(s.state, key + 8), text,
                               state == WL_KEYBOARD_KEY_STATE_PRESSED};
                    if (s.app->key)
                        s.app->key(e);
                    s.dirty = true;
                },
                [](void *d, wl_keyboard *, uint32_t, uint32_t depressed, uint32_t latched,
                   uint32_t locked, uint32_t group) {
                    auto &s = *static_cast<Impl *>(d);
                    if (s.state)
                        xkb_state_update_mask(s.state, depressed, latched, locked, 0, 0, group);
                },
                [](void *, wl_keyboard *, int32_t, int32_t) {}};
            wl_keyboard_add_listener(s.keyboard, &events, &s);
        }
        if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !s.touch) {
            s.touch = wl_seat_get_touch(seat);
            static const wl_touch_listener events = {
                [](void *d, wl_touch *, uint32_t, uint32_t, wl_surface *, int32_t, wl_fixed_t x,
                   wl_fixed_t y) {
                    auto &s = *static_cast<Impl *>(d);
                    s.px = wl_fixed_to_double(x);
                    s.py = wl_fixed_to_double(y);
                    s.notify_pointer(true);
                },
                [](void *d, wl_touch *, uint32_t, uint32_t, int32_t) {
                    static_cast<Impl *>(d)->notify_pointer(false);
                },
                [](void *d, wl_touch *, uint32_t, int32_t, wl_fixed_t x, wl_fixed_t y) {
                    auto &s = *static_cast<Impl *>(d);
                    s.px = wl_fixed_to_double(x);
                    s.py = wl_fixed_to_double(y);
                },
                [](void *, wl_touch *) {},
                [](void *d, wl_touch *) { static_cast<Impl *>(d)->notify_pointer(false); },
                [](void *, wl_touch *, int32_t, wl_fixed_t, wl_fixed_t) {},
                [](void *, wl_touch *, int32_t, wl_fixed_t) {}};
            wl_touch_add_listener(s.touch, &events, &s);
        }
    }
    Buffer *buffer() {
        for (auto i = buffers.begin(); i != buffers.end();) {
            auto &b = **i;
            if (!b.busy && (b.w != w || b.h != h)) {
                wl_buffer_destroy(b.buffer);
                munmap(b.data, b.size);
                i = buffers.erase(i);
            } else
                ++i;
        }
        for (auto &b : buffers)
            if (!b->busy)
                return b.get();
        if (buffers.size() >= 3)
            return nullptr;
        auto b = std::make_unique<Buffer>();
        b->owner = this;
        b->w = w;
        b->h = h;
        b->size = size_t(w) * h * 4;
        int fd = memfd_create("lmui-frame", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, off_t(b->size))) {
            if (fd >= 0)
                close(fd);
            throw std::runtime_error("cannot allocate LMUI frame");
        }
        b->data = mmap(nullptr, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (b->data == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("cannot map LMUI frame");
        }
        auto *pool = wl_shm_create_pool(shm, fd, int(b->size));
        b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        static const wl_buffer_listener events = {[](void *d, wl_buffer *) {
            auto *b = static_cast<Buffer *>(d);
            b->busy = false;
        }};
        wl_buffer_add_listener(b->buffer, &events, b.get());
        auto *out = b.get();
        buffers.push_back(std::move(b));
        return out;
    }
    void draw() {
        if (!configured || !dirty)
            return;
        auto *b = buffer();
        if (!b)
            return;
        auto *image = cairo_image_surface_create_for_data(static_cast<unsigned char *>(b->data),
                                                          CAIRO_FORMAT_ARGB32, w, h, w * 4);
        auto *cr = cairo_create(image);
        Canvas canvas(cr, w, h, Theme::material(app->dark));
        if (app->transparent) {
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            canvas.clear({0, 0, 0, 0});
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        } else
            canvas.clear(canvas.theme.background);
        if (app->paint)
            app->paint(canvas);
        cairo_destroy(cr);
        cairo_surface_flush(image);
        if (const char *capture = getenv("LMUI_CAPTURE_FRAME")) {
            cairo_status_t status = cairo_surface_write_to_png(image, capture);
            if (status != CAIRO_STATUS_SUCCESS)
                std::cerr << "LMUI capture: " << cairo_status_to_string(status) << '\n';
            unsetenv("LMUI_CAPTURE_FRAME");
        }
        if (getenv("LMUI_QUIT_AFTER_FRAME"))
            running = false;
        cairo_surface_destroy(image);
        wl_surface_attach(surface, b->buffer, 0, 0);
        wl_surface_damage(surface, 0, 0, w, h);
        wl_surface_commit(surface);
        b->busy = true;
        dirty = false;
    }
    int run() {
        display = wl_display_connect(nullptr);
        if (!display)
            throw std::runtime_error("cannot connect to Wayland display");
        registry = wl_display_get_registry(display);
        static const wl_registry_listener globals = {global,
                                                     [](void *, wl_registry *, uint32_t) {}};
        wl_registry_add_listener(registry, &globals, this);
        if (wl_display_roundtrip(display) < 0 || !compositor || !shm || !wm)
            throw std::runtime_error("Wayland compositor lacks required globals");
        surface = wl_compositor_create_surface(compositor);
        xdg = xdg_wm_base_get_xdg_surface(wm, surface);
        static const xdg_surface_listener surface_events = {
            [](void *d, xdg_surface *surface, uint32_t serial) {
                auto &s = *static_cast<Impl *>(d);
                xdg_surface_ack_configure(surface, serial);
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
};
Application::Application(std::string id, std::string title)
    : impl(std::make_unique<Impl>(this, std::move(id), std::move(title))) {}
Application::~Application() = default;
int Application::run() {
    try {
        return impl->run();
    } catch (const std::exception &e) {
        std::cerr << "LMUI: " << e.what() << '\n';
        return 1;
    }
}
void Application::redraw() { impl->dirty = true; }
void Application::quit() { impl->running = false; }
void Application::input_region(const std::vector<Rect> &rectangles) {
    if (!impl->surface)
        return;
    auto *region = wl_compositor_create_region(impl->compositor);
    for (auto r : rectangles)
        wl_region_add(region, int(r.x), int(r.y), int(r.w), int(r.h));
    wl_surface_set_input_region(impl->surface, region);
    wl_region_destroy(region);
}
int Application::width() const { return impl->w; }
int Application::height() const { return impl->h; }
} // namespace lmui
