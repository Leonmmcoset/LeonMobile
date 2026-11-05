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
        if (!root_prompt.empty() && page != Page::lock)
            return;
        volume_gesture = shade.visible() && !power_menu && volume_track.contains(x, y);
        notification_gesture = shade.visible() && !power_menu && !notifications.empty() &&
                               notification_bounds.contains(x, y);
        notification_start = notification_scroll;
        shade_gesture = page != Page::lock && !power_menu && !volume_gesture &&
                        !notification_gesture &&
                        (y < 42 || (shade.visible() && y < shade.height(app.height())));
        if (shade_gesture) {
            shade.begin(y, app.height());
            shade_focused = true;
            control("shell");
        }
        if (volume_gesture)
            motion(x, y);
        return;
    }
    if (!pressed)
        return;
    pressed = false;
    double dx = x - down_x, dy = y - down_y;
    if (!root_prompt.empty() && page != Page::lock) {
        touch_feedback.handle({x, y, PointerPhase::up}, hits);
        app.redraw();
        return;
    }
    if (volume_gesture) {
        touch_feedback.cancel();
        volume_gesture = false;
        try {
            set_preference("volume", volume_preview);
        } catch (const std::exception &e) {
            error = e.what();
        }
        volume_preview = -1;
        app.redraw();
        return;
    }
    if (shade_gesture) {
        shade_gesture = false;
        if (shade.release(y, app.height())) {
            touch_feedback.cancel();
            app.redraw();
            return;
        }
    }
    if (notification_gesture) {
        notification_gesture = false;
        if (std::abs(dy) > 8) {
            touch_feedback.cancel();
            app.redraw();
            return;
        }
    }
    if (page == Page::lock) {
        if (unlock_entry && std::abs(dx) < 16 && std::abs(dy) < 16)
            touch_feedback.handle({x, y, PointerPhase::up}, hits);
        else if (dy < -60 ||
                 (down_y > app.height() - 220 && std::abs(dx) < 16 && std::abs(dy) < 16))
            unlock_start();
        lock_drag = 0;
        touch_feedback.cancel();
        app.redraw();
        return;
    }
    if (prefs["navigation"] == "gestures" && down_y > app.height() - 64 && dy < -50) {
        touch_feedback.cancel();
        if (shade.visible()) {
            close_shade();
            return;
        }
        if (dy < -190)
            show_page(Page::tasks);
        else
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
    if (volume_gesture) {
        volume_preview =
            int(std::lround(std::clamp((x - volume_track.x) / volume_track.w, 0.0, 1.0) * 100));
    } else if (notification_gesture)
        notification_scroll = std::clamp(notification_start + down_y - y, 0.0, notification_limit);
    else if (shade_gesture)
        shade.move(y, app.height());
    touch_feedback.handle({x, y, PointerPhase::move}, hits);
    app.redraw();
}
} // namespace leon::shell
