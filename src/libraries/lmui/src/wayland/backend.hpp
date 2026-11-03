#pragma once
#include "xdg-shell-client-protocol.h"
#include <chrono>
#include <lmui/lmui.hpp>
#include <sys/mman.h>
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
    std::chrono::steady_clock::time_point opened{};
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
    ~Impl();
    void notify_pointer(bool down);
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface,
                       uint32_t version);
    static void capabilities(void *data, wl_seat *seat, uint32_t caps);
    Buffer *buffer();
    void draw();
    int run();
};
} // namespace lmui
