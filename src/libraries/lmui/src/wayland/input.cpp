#include "backend.hpp"
#include <algorithm>
#include <cstring>
#include <linux/input-event-codes.h>
#include <unistd.h>
namespace lmui {
unsigned Application::Impl::key_modifiers() const {
    if (!state)
        return 0;
    unsigned result = 0;
    for (const auto &[name, flag] :
         {std::pair{XKB_MOD_NAME_SHIFT, key_shift}, std::pair{XKB_MOD_NAME_ALT, key_alt},
          std::pair{XKB_MOD_NAME_CTRL, key_control}, std::pair{XKB_MOD_NAME_LOGO, key_super}})
        if (xkb_state_mod_name_is_active(state, name, XKB_STATE_MODS_EFFECTIVE) > 0)
            result |= flag;
    return result;
}
void Application::Impl::dispatch_key(uint32_t key, bool pressed) {
    if (!state)
        return;
    char text[128]{};
    xkb_state_key_get_utf8(state, key + 8, text, sizeof(text));
    KeyEvent event{xkb_state_key_get_one_sym(state, key + 8), text, pressed};
    if (app->key)
        app->key(event);
    dirty = true;
}
void Application::Impl::repeat_dispatch() {
    if (!repeat_key || repeat_rate <= 0 || !keyboard_focused)
        return;
    auto now = std::chrono::steady_clock::now();
    if (now >= repeat_due) {
        dispatch_key(repeat_key, true);
        repeat_due = now + std::chrono::milliseconds(std::max(1, 1000 / repeat_rate));
    }
}
void Application::Impl::notify_pointer(PointerPhase phase) {
    if (phase == PointerPhase::move && !pointer_down)
        return;
    pointer_down = phase == PointerPhase::down || (phase == PointerPhase::move && pointer_down);
    if (event_callback)
        event_callback({px, py, phase});
    if (app->pointer && (phase == PointerPhase::down || phase == PointerPhase::up))
        app->pointer(px, py, phase == PointerPhase::down);
    dirty = true;
}
void Application::Impl::notify_motion(double x, double y) {
    px = x;
    py = y;
    if (motion_callback) {
        motion_callback(px, py);
        dirty = true;
    }
    notify_pointer(PointerPhase::move);
}
void Application::Impl::capabilities(void *data, wl_seat *seat, uint32_t caps) {
    auto &s = *static_cast<Impl *>(data);
    if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s.pointer) {
        s.notify_pointer(PointerPhase::cancel);
        wl_pointer_destroy(s.pointer);
        s.pointer = nullptr;
    }
    if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && s.touch) {
        s.notify_pointer(PointerPhase::cancel);
        s.touch_id = -1;
        wl_touch_destroy(s.touch);
        s.touch = nullptr;
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s.pointer) {
        s.pointer = wl_seat_get_pointer(seat);
        static const wl_pointer_listener events = {
            [](void *d, wl_pointer *, uint32_t serial, wl_surface *, wl_fixed_t x, wl_fixed_t y) {
                auto &s = *static_cast<Impl *>(d);
                s.input_serial = serial;
                s.px = wl_fixed_to_double(x);
                s.py = wl_fixed_to_double(y);
            },
            [](void *d, wl_pointer *, uint32_t, wl_surface *) {
                auto &s = *static_cast<Impl *>(d);
                if (s.touch_id < 0)
                    s.notify_pointer(PointerPhase::cancel);
            },
            [](void *d, wl_pointer *, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                auto &s = *static_cast<Impl *>(d);
                if (s.touch_id < 0)
                    s.notify_motion(wl_fixed_to_double(x), wl_fixed_to_double(y));
            },
            [](void *d, wl_pointer *, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
                auto &s = *static_cast<Impl *>(d);
                s.input_serial = serial;
                if (button == BTN_LEFT && s.touch_id < 0)
                    s.notify_pointer(state == WL_POINTER_BUTTON_STATE_PRESSED ? PointerPhase::down
                                                                              : PointerPhase::up);
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
            [](void *, wl_pointer *, uint32_t, int32_t) {}
#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
            ,
            [](void *, wl_pointer *, uint32_t, int32_t) {}
#endif
#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
            ,
            [](void *, wl_pointer *, uint32_t, uint32_t) {}
#endif
        };
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
            [](void *d, wl_keyboard *, uint32_t serial, wl_surface *, wl_array *) {
                auto &s = *static_cast<Impl *>(d);
                s.input_serial = serial;
                s.keyboard_focused = true;
                s.dirty = true;
            },
            [](void *d, wl_keyboard *, uint32_t, wl_surface *) {
                auto &s = *static_cast<Impl *>(d);
                s.keyboard_focused = false;
                s.repeat_key = 0;
                s.dirty = true;
            },
            [](void *d, wl_keyboard *, uint32_t serial, uint32_t, uint32_t key, uint32_t state) {
                auto &s = *static_cast<Impl *>(d);
                s.input_serial = serial;
                bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
                s.dispatch_key(key, pressed);
                if (pressed && s.keymap && xkb_keymap_key_repeats(s.keymap, key + 8)) {
                    s.repeat_key = key;
                    s.repeat_due = std::chrono::steady_clock::now() +
                                   std::chrono::milliseconds(s.repeat_delay);
                } else if (!pressed && s.repeat_key == key)
                    s.repeat_key = 0;
            },
            [](void *d, wl_keyboard *, uint32_t, uint32_t depressed, uint32_t latched,
               uint32_t locked, uint32_t group) {
                auto &s = *static_cast<Impl *>(d);
                if (s.state)
                    xkb_state_update_mask(s.state, depressed, latched, locked, 0, 0, group);
            },
            [](void *d, wl_keyboard *, int32_t rate, int32_t delay) {
                auto &s = *static_cast<Impl *>(d);
                s.repeat_rate = std::clamp(rate, 0, 100);
                s.repeat_delay = std::clamp(delay, 0, 5000);
            }};
        wl_keyboard_add_listener(s.keyboard, &events, &s);
    }
    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !s.touch) {
        s.touch = wl_seat_get_touch(seat);
        static const wl_touch_listener events = {
            [](void *d, wl_touch *, uint32_t serial, uint32_t, wl_surface *, int32_t id,
               wl_fixed_t x, wl_fixed_t y) {
                auto &s = *static_cast<Impl *>(d);
                s.input_serial = serial;
                if (s.touch_id >= 0)
                    return;
                s.notify_pointer(PointerPhase::cancel);
                s.touch_id = id;
                s.px = wl_fixed_to_double(x);
                s.py = wl_fixed_to_double(y);
                s.notify_pointer(PointerPhase::down);
            },
            [](void *d, wl_touch *, uint32_t, uint32_t, int32_t id) {
                auto &s = *static_cast<Impl *>(d);
                if (id == s.touch_id) {
                    s.notify_pointer(PointerPhase::up);
                    s.touch_id = -1;
                }
            },
            [](void *d, wl_touch *, uint32_t, int32_t id, wl_fixed_t x, wl_fixed_t y) {
                auto &s = *static_cast<Impl *>(d);
                if (id != s.touch_id)
                    return;
                s.notify_motion(wl_fixed_to_double(x), wl_fixed_to_double(y));
            },
            [](void *, wl_touch *) {},
            [](void *d, wl_touch *) {
                auto &s = *static_cast<Impl *>(d);
                s.notify_pointer(PointerPhase::cancel);
                s.touch_id = -1;
            },
            [](void *, wl_touch *, int32_t, wl_fixed_t, wl_fixed_t) {},
            [](void *, wl_touch *, int32_t, wl_fixed_t) {}};
        wl_touch_add_listener(s.touch, &events, &s);
    }
}
} // namespace lmui
