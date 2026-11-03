#include "application.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
using namespace mobile;
namespace leon::apps {
TerminalApplication::TerminalApplication()
    : ApplicationWindow("org.leon.terminal", "终端", "Terminal") {}
void TerminalApplication::on_tick() { app.redraw(); }
void TerminalApplication::text_input(const std::string &text) { terminal.input(text); }
bool TerminalApplication::key_event(const KeyEvent &event) {
    if (event.symbol == XKB_KEY_Escape && !keyboard) {
        terminal.input("\033");
        return true;
    }
    switch (event.symbol) {
    case XKB_KEY_Up:
        terminal.input("\033[A");
        return true;
    case XKB_KEY_Down:
        terminal.input("\033[B");
        return true;
    case XKB_KEY_Right:
        terminal.input("\033[C");
        return true;
    case XKB_KEY_Left:
        terminal.input("\033[D");
        return true;
    case XKB_KEY_Tab:
        terminal.input("\t");
        return true;
    default:
        return false;
    }
}
void TerminalApplication::paint_content(Canvas &c, double bottom) {
    double w = c.width;
    terminal.draw(c, {16, 84, w - 32, bottom - 148});
    button(c, hits, {20, bottom - 52, w - 40, 44}, tr(en, "键盘", "Keyboard"),
           [&] { keyboard = !keyboard; });
}
} // namespace leon::apps
