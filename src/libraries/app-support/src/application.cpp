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
        scroll = std::max(0.0, scroll + delta);
        app.redraw();
    };
    app.pointer = [this](double x, double y, bool down) {
        if (down)
            perform([&] { activate(hits, x, y); });
    };
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
void ApplicationWindow::paint(Canvas &c) {
    hits.clear();
    auto t = c.theme;
    double w = c.width, h = c.height;
    c.text(tr(en, chinese_, english_), {24, 12, w - 100, 58}, 30, t.text, true);
    button(c, hits, {w - 64, 22, 40, 40}, "×", [this] { app.quit(); });
    double bottom = keyboard ? h - 250 : h - 16;
    paint_content(c, bottom);
    if (!error.empty()) {
        c.rounded({16, bottom - 108, w - 32, 48}, t.container, 16);
        c.text(error, {28, bottom - 108, w - 56, 48}, 12, t.text);
    }
    if (keyboard)
        keyboard_.draw(
            c, hits, keyboard, en, [this](std::string text) { text_input(text); }, enter_text());
}
} // namespace mobile
