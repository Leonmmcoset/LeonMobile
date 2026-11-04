#include <cassert>
#include <iostream>
#include <lmui/lmui.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
int main() {
    lmui::Application app("org.leon.input-test", "LMUI input test");
    std::string clipboard(256 * 1024, 'x');
    clipboard += " 中文\n";
    bool control{}, alt{}, shift{}, copied{}, pasted{};
    int repeats = 0, ticks = 0;
    app.paint = [](lmui::Canvas &canvas) { canvas.clear(canvas.theme.background); };
    app.tick = [&] {
        if (++ticks > 100)
            app.quit();
    };
    app.key = [&](const lmui::KeyEvent &key) {
        if (!key.pressed)
            return;
        auto modifiers = app.key_modifiers();
        if (key.symbol == XKB_KEY_k)
            control = modifiers & lmui::key_control;
        if (key.symbol == XKB_KEY_a)
            alt = modifiers & lmui::key_alt;
        if (key.symbol == XKB_KEY_Right)
            shift = modifiers & lmui::key_shift;
        if (key.symbol == XKB_KEY_c)
            copied = app.copy_text(clipboard);
        if (key.symbol == XKB_KEY_v)
            app.request_clipboard([&](std::string text) { pasted = text == clipboard; });
        if (key.symbol == XKB_KEY_x)
            ++repeats;
        if (key.symbol == XKB_KEY_Return) {
            assert(app.focused() && control && alt && shift && copied && pasted && repeats > 2);
            std::cout << "Wayland input: modifiers, repeat and 256 KiB UTF-8 clipboard passed\n";
            app.quit();
        }
    };
    assert(!app.run());
    assert(ticks <= 100 && pasted);
}
