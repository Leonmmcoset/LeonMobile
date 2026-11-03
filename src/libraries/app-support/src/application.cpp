#include <cmath>
#include <leon/mobile/application.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
namespace mobile {
ApplicationWindow::ApplicationWindow(std::string id, std::string chinese, std::string english)
    : app(std::move(id), english), chinese_(std::move(chinese)), english_(std::move(english)) {
    fs::create_directories(user_home());
    poll_preferences();
    app.tick = [this] {
        poll_preferences();
        on_tick();
    };
    app.key = [this](const KeyEvent &e) {
        if (!e.pressed || key_event(e))
            return;
        if (e.symbol == XKB_KEY_Escape)
            keyboard = false;
        else if (e.symbol == XKB_KEY_BackSpace)
            text_input("\b");
        else if (e.symbol == XKB_KEY_Return)
            text_input(enter_text());
        else if (!e.text.empty())
            text_input(e.text);
    };
    app.scroll = [this](double delta) {
        touch_.cancel();
        scroll = std::max(0.0, scroll + delta);
        app.redraw();
    };
    app.pointer_event = [this](const PointerEvent &event) { pointer(event); };
    app.paint = [this](Canvas &c) { paint(c); };
}
int ApplicationWindow::run() { return app.run(); }
void ApplicationWindow::perform(const std::function<void()> &action) {
    try {
        action();
        error.clear();
    } catch (const std::exception &e) {
        error = e.what();
    }
    app.redraw();
}
void ApplicationWindow::poll_preferences() {
    try {
        auto previous = prefs;
        prefs = rpc({{"op", "settings"}}).at("settings");
        en = prefs["language"] == "en";
        if (!theme_override)
            app.dark = prefs.value("dark", false);
        app.reduce_motion = prefs.value("reduce_motion", false);
        if (prefs != previous)
            app.redraw();
    } catch (...) {
    }
}
void ApplicationWindow::set(std::string key, Json value) {
    perform([&] {
        prefs = rpc({{"op", "set"}, {"key", key}, {"value", value}}).at("settings");
        poll_preferences();
    });
}
void ApplicationWindow::edit(std::string &target, const std::string &text) {
    if (text == "\b")
        backspace(target);
    else if (target.size() + text.size() <= 1024 * 1024)
        target += text;
}
void ApplicationWindow::paint_header(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    c.text(tr(en, chinese_, english_), {24, 12, w - 100, 58}, 30, t.text, true);
    button(c, hits, {w - 64, 22, 40, 40}, "×", [this] { app.quit(); });
}
void ApplicationWindow::paint(Canvas &c) {
    hits.clear();
    auto t = c.theme;
    double w = c.width, h = c.height;
    double keyboard_fraction = c.animate("window/keyboard", keyboard ? 1 : 0, motion::enter);
    double bottom = h - 16 - 234 * keyboard_fraction;
    content_transition_.begin(c);
    paint_header(c);
    double target_scroll = scroll;
    scroll = c.animate("window/scroll/" + std::to_string(content_revision_), target_scroll,
                       scroll_dragging_ ? MotionSpec{0} : motion::settle);
    double presented_scroll = scroll;
    paint_content(c, bottom);
    if (scroll == presented_scroll)
        scroll = target_scroll;
    content_transition_.end(c);
    translate_hits(hits, 0, content_transition_.offset_x(), content_transition_.offset_y(),
                   {0, 0, w, h});
    if (!error.empty())
        shown_error_ = error;
    double message = c.animate("window/message", error.empty() ? 0 : 1, motion::settle, 0);
    if (message > 0) {
        c.save();
        cairo_push_group(c.cr);
        double y = bottom - 108 + 12 * (1 - message);
        c.rounded({16, y, w - 32, 48}, t.container, 16);
        c.text(shown_error_, {28, y, w - 56, 48}, 12, t.text);
        cairo_pop_group_to_source(c.cr);
        cairo_paint_with_alpha(c.cr, message);
        c.restore();
    }
    if (keyboard_fraction > 0) {
        size_t first_key = hits.size();
        c.save();
        c.clip({0, h - 246 * keyboard_fraction, w, 246 * keyboard_fraction});
        double dy = 246 * (1 - keyboard_fraction);
        cairo_translate(c.cr, 0, dy);
        keyboard_.draw(
            c, hits, keyboard, en, [this](std::string text) { text_input(text); }, enter_text());
        c.restore();
        if (keyboard)
            translate_hits(hits, first_key, 0, dy, {0, h - 246 * keyboard_fraction, w, h});
        else
            hits.resize(first_key);
    }
    touch_.draw(c);
}
void ApplicationWindow::transition_content(double dx, double dy) {
    touch_.cancel();
    scroll_eligible_ = scroll_dragging_ = false;
    ++content_revision_;
    content_transition_.start(dx, dy);
    app.redraw();
}
void ApplicationWindow::pointer(const PointerEvent &e) {
    perform([&] {
        if (e.phase == PointerPhase::down) {
            scroll_eligible_ = can_scroll(e.x, e.y) && (!keyboard || e.y < app.height() - 246);
            scroll_dragging_ = false;
            scroll_start_ = scroll_last_ = scroll;
            down_y_ = e.y;
            scroll_time_ = motion_time();
            scroll_velocity_ = 0;
        } else if (e.phase == PointerPhase::cancel) {
            scroll_eligible_ = scroll_dragging_ = false;
        } else if (scroll_eligible_ && !touch_.continuous() && e.phase == PointerPhase::move) {
            if (std::abs(e.y - down_y_) > 12)
                scroll_dragging_ = true;
            if (scroll_dragging_) {
                touch_.cancel();
                scroll = std::max(0.0, scroll_start_ + down_y_ - e.y);
                double now = motion_time();
                if (now - scroll_time_ > .001)
                    scroll_velocity_ = (scroll - scroll_last_) / (now - scroll_time_);
                scroll_last_ = scroll;
                scroll_time_ = now;
            }
        } else if (e.phase == PointerPhase::up && scroll_dragging_) {
            if (motion_time() - scroll_time_ < .08)
                scroll = std::max(0.0, scroll + std::clamp(scroll_velocity_ * .12, -200.0, 200.0));
            scroll_eligible_ = scroll_dragging_ = false;
            return;
        }
        touch_.handle(e, hits);
    });
}
} // namespace mobile
