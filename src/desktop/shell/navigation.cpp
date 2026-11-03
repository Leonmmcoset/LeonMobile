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
        touch_feedback.handle({x, y, PointerPhase::down}, hits);
        return;
    }
    if (!pressed)
        return;
    pressed = false;
    double dx = x - down_x, dy = y - down_y;
    if (page == Page::lock) {
        if (dy < -60 || std::hypot(dx, dy) < 12)
            home();
        lock_drag = 0;
        app.redraw();
        return;
    }
    if (down_y < 42 && dy > 55) {
        touch_feedback.cancel();
        page = x > app.width() / 2 ? Page::quick : Page::notifications;
        control("shell");
        app.redraw();
        return;
    }
    if (prefs["navigation"] == "gestures" && down_y > app.height() - 64 && dy < -50) {
        touch_feedback.cancel();
        if (dy < -190) {
            page = Page::tasks;
            control("shell");
        } else
            home();
        app.redraw();
        return;
    }
    try {
        touch_feedback.handle({x, y, PointerPhase::up}, hits);
    } catch (const std::exception &e) {
        error = e.what();
    }
    app.redraw();
}
void Shell::motion(double x, double y) {
    if (!pressed)
        return;
    if (page == Page::lock)
        lock_drag = std::clamp(down_y - y, 0.0, app.height() * .35);
    touch_feedback.handle({x, y, PointerPhase::move}, hits);
    app.redraw();
}
} // namespace leon::shell
