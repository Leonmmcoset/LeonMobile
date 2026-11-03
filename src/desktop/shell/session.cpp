#include "control.hpp"
#include "shell.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
using namespace mobile;
namespace leon::shell {
Shell::Shell() {
    app.transparent = true;
    refresh();
    app.tick = [this] {
        if (++ticks % 5 == 0) {
            refresh();
            if (page == Page::active && !active_id.empty() && !running.contains(active_id))
                home();
            app.redraw();
        }
    };
    app.key = [this](const KeyEvent &e) {
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
    app.pointer = [this](double x, double y, bool down) { pointer(x, y, down); };
    app.on_pointer_motion([this](double x, double y) { motion(x, y); });
    app.scroll = [this](double delta) {
        if (shade.visible()) {
            notification_scroll = std::clamp(notification_scroll + delta, 0.0, notification_limit);
            app.redraw();
        }
    };
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

    try {
        auto s = rpc({{"op", "list"}});
        apps = s.at("apps");
        running = s.at("running");
        prefs = rpc({{"op", "settings"}}).at("settings");
        notifications = rpc({{"op", "notifications"}}).at("notifications");
        app.dark = prefs.value("dark", false);
        en = prefs["language"] == "en";
        error.clear();
    } catch (const std::exception &e) {
        error = e.what();
    }
}
void Shell::home() {
    reset_shade();
    page = Page::home;
    active_id.clear();
    control("shell");
    app.redraw();
}
void Shell::launch(std::string id) {
    try {
        rpc({{"op", "launch"}, {"id", id}});
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
    shade.reset();
    shade_focused = shade_gesture = volume_gesture = notification_gesture = power_menu = false;
    volume_preview = -1;
    notification_scroll = 0;
}
void Shell::set_preference(const std::string &key, Json value) {
    prefs = rpc({{"op", "set"}, {"key", key}, {"value", std::move(value)}}).at("settings");
    app.dark = prefs.value("dark", false);
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
void Shell::shade_click(Rect bounds, std::function<void()> action) {
    double bottom = std::min(bounds.y + bounds.h, shade.height(app.height()) - 24);
    bounds.y = std::max(42.0, bounds.y);
    bounds.h = bottom - bounds.y;
    if (bounds.h > 0)
        click(hits, bounds, std::move(action));
}
} // namespace leon::shell
