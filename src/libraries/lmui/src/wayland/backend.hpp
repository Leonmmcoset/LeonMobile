#pragma once
#include "text-input-client-protocol.h"
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
    wl_callback *frame_callback{};
    xdg_surface *xdg{};
    xdg_toplevel *toplevel{};
    xkb_context *xkb{};
    xkb_keymap *keymap{};
    xkb_state *state{};
    bool keyboard_focused{};
    uint32_t input_serial{}, repeat_key{};
    int repeat_rate{}, repeat_delay{};
    std::chrono::steady_clock::time_point repeat_due{};
    void dispatch_key(uint32_t key, bool pressed);
    void repeat_dispatch();
    unsigned key_modifiers() const;
    struct Clipboard;
    std::shared_ptr<Clipboard> clipboard;
    wl_data_device_manager *data_manager{};
    void clipboard_open();
    void clipboard_close();
    void clipboard_tick();
    bool clipboard_copy(const std::string &);
    void clipboard_request(std::function<void(std::string)>);
    int w{480}, h{960};
    double px{}, py{};
    int32_t touch_id{-1};
    bool pointer_down{}, force_reduced{}, reduce_motion{};
    MotionContext motion;
    std::function<void(double, double)> motion_callback;
    std::function<void(const PointerEvent &)> event_callback;
    zwp_text_input_manager_v3 *text_manager{};
    zwp_text_input_v3 *text_input{};
    bool text_entered{}, text_wanted{}, text_enabled{}, text_private{}, text_private_sent{};
    uint32_t text_serial{}, delete_before{}, delete_after{};
    std::string pending_text, pending_preedit;
    bool got_text{}, got_preedit{}, got_delete{};
    std::function<void(const std::string &)> text_commit, text_preedit;
    std::function<void(uint32_t, uint32_t)> text_delete;
    void text_open();
    void text_update();
    struct FdWatch {
        int fd;
        short events;
        uint64_t generation;
        std::function<void()> callback;
    };
    uint64_t watch_generation{};
    std::vector<FdWatch> watched_fds;
    int lifecycle_fd{-1};
    std::function<bool(LifecycleEvent)> lifecycle_callback;
    void lifecycle_open();
    void lifecycle_dispatch();
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
    void notify_pointer(PointerPhase phase);
    void notify_motion(double x, double y);
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface,
                       uint32_t version);
    static void capabilities(void *data, wl_seat *seat, uint32_t caps);
    Buffer *buffer();
    void draw();
    int run();
};
} // namespace lmui
