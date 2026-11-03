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
        if (e.pressed && e.symbol == XKB_KEY_Escape)
            home();
    };
    app.pointer = [this](double x, double y, bool down) { pointer(x, y, down); };
    app.paint = [this](Canvas &canvas) { paint(canvas); };
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

    page = Page::home;
    active_id.clear();
    control("shell");
    app.redraw();
}
void Shell::launch(std::string id) {

    try {
        rpc({{"op", "launch"}, {"id", id}});
        active_id = id;
        page = Page::active;
        control("focus " + id);
        refresh();
    } catch (const std::exception &e) {
        error = e.what();
    }
    app.redraw();
}
} // namespace leon::shell
