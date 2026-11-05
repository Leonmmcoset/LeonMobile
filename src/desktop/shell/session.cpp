#include "control.hpp"
#include "shell.hpp"
#include <cmath>
#include <xkbcommon/xkbcommon-keysyms.h>
using namespace mobile;
namespace leon::shell {
Shell::Shell() {
    device_secret.reserve(128);
    control("lock");
    control("unmodal");
    app.transparent = true;
    refresh();
    app.tick = [this] {
        authentication_refresh();
        if (++ticks % 5 == 0) {
            refresh();
            if (page == Page::active && !active_id.empty() &&
                (!running.contains(active_id) ||
                 (app_states.contains(active_id) &&
                  app_states.at(active_id).value("stopping", false))))
                home();
            app.redraw();
        }
    };
    app.key = [this](const KeyEvent &e) {
        if (page == Page::lock) {
            if (e.pressed) {
                if (e.symbol == XKB_KEY_Escape) {
                    clear_device_secret();
                    unlock_entry = false;
                } else if (e.symbol == XKB_KEY_Return || e.symbol == XKB_KEY_KP_Enter)
                    unlock_start();
                else if (e.symbol == XKB_KEY_BackSpace)
                    unlock_text("\b");
                else if (!e.text.empty()) {
                    unlock_entry = true;
                    unlock_text(e.text);
                }
                app.redraw();
            }
            return;
        }
        if (!root_prompt.empty()) {
            if (e.pressed) {
                if (e.symbol == XKB_KEY_Escape)
                    root_cancel();
                else if (e.symbol == XKB_KEY_Return || e.symbol == XKB_KEY_KP_Enter)
                    root_text("\n");
                else if (e.symbol == XKB_KEY_BackSpace)
                    root_text("\b");
                else if (!e.text.empty())
                    root_text(e.text);
            }
            return;
        }
        if (e.pressed && e.symbol == XKB_KEY_Escape) {
            if (power_menu) {
                power_menu = false;
                app.redraw();
            } else if (shade.visible())
                close_shade();
            else
                home();
        }
    };
    app.on_pointer_event([this](const PointerEvent &event) {
        if (event.phase == PointerPhase::cancel) {
            if (shade_gesture)
                shade.show(
                    static_cast<Shade::Level>(std::clamp(int(std::lround(shade.extent())), 0, 2)));
            pressed = shade_gesture = volume_gesture = notification_gesture = false;
            lock_drag = 0;
            volume_preview = -1;
            touch_feedback.cancel();
            app.redraw();
        } else if (event.phase == PointerPhase::move)
            motion(event.x, event.y);
        else
            pointer(event.x, event.y, event.phase == PointerPhase::down);
    });
    app.scroll = [this](double delta) {
        if (shade.visible()) {
            notification_scroll = std::clamp(notification_scroll + delta, 0.0, notification_limit);
            app.redraw();
        }
    };
    app.on_text_input([this](const std::string &text) {
        if (!root_prompt.empty())
            root_text(text);
        else
            unlock_text(text);
    });
    app.paint = [this](Canvas &canvas) { paint(canvas); };
    if (getenv("LMUI_CAPTURE_FRAME")) {
        const char *preview = getenv("LEON_SHELL_CAPTURE_SHADE");
        if (preview && (std::string(preview) == "compact" || std::string(preview) == "expanded")) {
            page = Page::home;
            auto now = Shade::Clock::now();
            shade.show(std::string(preview) == "expanded" ? Shade::Level::expanded
                                                          : Shade::Level::compact,
                       now);
            shade.advance(now + std::chrono::seconds(1));
        }
    }
}
int Shell::run() { return app.run(); }
void Shell::refresh() {
    synchronize_background();
    system_refresh();

    try {
        auto s = rpc({{"op", "list"}});
        apps = s.at("apps");
        running = s.at("running");
        app_states = s.at("applications");
        prefs = rpc({{"op", "settings"}}).at("settings");
        app.dark = prefs.value("dark", false);
        app.reduce_motion(prefs.value("reduce_motion", false));
        en = prefs["language"] == "en";
        if (!background_pending)
            error.clear();
    } catch (const std::exception &e) {
        error = e.what();
    }
    try {
        auto state=notification_rpc({{"op", "notifications"}});
        notifications=state.at("notifications");
        notifications_lock_epoch=state.value("privacy_locked",false)?state.value("lock_epoch",""):"";
        notifications_available = true;
    } catch (const std::exception &) {
        notifications = Json::array();
        notifications_lock_epoch.clear();
        notifications_available = false;
    }
}
void Shell::synchronize_background() {
    if (!background_pending)
        return;
    try {
        rpc({{"op", "background"}});
        background_pending = false;
    } catch (const std::exception &e) {
        error = e.what();
    }
}
void Shell::show_page(Page destination) {
    if (destination != Page::lock && page == Page::lock &&
        (!authentication.ok || authentication.locked))
        return;
    if (destination == Page::lock) {
        notifications=Json::array();notifications_lock_epoch.clear();
        root_clear_prompt();
        clear_device_secret();
        unlock_entry = unlock_pending = false;
        ready_epoch.clear();
        control("lock");
    }
    reset_shade();
    page = destination;
    active_id.clear();
    control("shell");
    // Lifecycle changes belong to navigation, even if no frame is painted
    // between leaving an app and selecting the next one. Retry lost requests.
    background_pending = true;
    synchronize_background();
    app.redraw();
}
void Shell::home() { show_page(Page::home); }
void Shell::launch(std::string id) {
    if (page == Page::lock || !authentication.ok || authentication.locked)
        return;
    try {
        rpc({{"op", "launch"}, {"id", id}});
        background_pending = false;
        reset_shade();
        active_id = id;
        page = Page::active;
        control("focus " + id);
        refresh();
    } catch (const std::exception &e) {
        error = e.what();
    }
    app.redraw();
}
void Shell::dismiss_task(const std::string &id) {
    auto response = rpc({{"op", "close"}, {"id", id}});
    app_states = response.at("applications");
    if (!app_states.contains(id))
        running.erase(id);
    refresh();
    app.redraw();
}
void Shell::open_shade(Shade::Level level) {
    if (page == Page::lock)
        return;
    shade.show(level);
    shade_focused = true;
    control("shell");
    app.redraw();
}
void Shell::close_shade() {
    power_menu = false;
    shade.show(Shade::Level::closed);
    app.redraw();
}
void Shell::reset_shade() {
    touch_feedback.cancel();
    shade.reset();
    shade_focused = shade_gesture = volume_gesture = notification_gesture = power_menu = false;
    volume_preview = -1;
    notification_scroll = 0;
}
void Shell::set_preference(const std::string &key, Json value) {
    prefs = rpc({{"op", "set"}, {"key", key}, {"value", std::move(value)}}).at("settings");
    app.dark = prefs.value("dark", false);
    app.reduce_motion(prefs.value("reduce_motion", false));
    en = prefs.value("language", "zh-CN") == "en";
    error.clear();
    app.redraw();
}
void Shell::toggle_mute() {
    int volume = prefs.value("volume", 70);
    if (volume > 0)
        unmuted_volume = volume;
    set_preference("volume", volume > 0 ? 0 : unmuted_volume);
}
void Shell::shade_click(Rect bounds, std::function<void()> action, bool prominent) {
    double bottom = std::min(bounds.y + bounds.h, shade.height(app.height()) - 24);
    bounds.y = std::max(42.0, bounds.y);
    bounds.h = bottom - bounds.y;
    if (bounds.h > 0) {
        click(hits, bounds, std::move(action));
        hits.back().prominent = prominent;
    }
}
} // namespace leon::shell
