#include "control.hpp"
#include "shell.hpp"
#include <cmath>
using namespace mobile;
namespace leon::shell {
void Shell::pointer(double x, double y, bool down) {

    if (down) {
        pressed = true;
        down_x = x;
        down_y = y;
        return;
    }
    if (!pressed)
        return;
    pressed = false;
    double dx = x - down_x, dy = y - down_y;
    if (page == Page::lock) {
        if (dy < -60 || std::abs(dx) < 12)
            home();
        return;
    }
    if (down_y < 42 && dy > 55) {
        page = x > app.width() / 2 ? Page::quick : Page::notifications;
        control("shell");
        app.redraw();
        return;
    }
    if (prefs["navigation"] == "gestures" && down_y > app.height() - 64 && dy < -50) {
        if (dy < -190) {
            page = Page::tasks;
            control("shell");
        } else
            home();
        app.redraw();
        return;
    }
    if (std::abs(dx) < 16 && std::abs(dy) < 16) {
        try {
            activate(hits, x, y);
        } catch (const std::exception &e) {
            error = e.what();
        }
    }
    app.redraw();
}
} // namespace leon::shell
