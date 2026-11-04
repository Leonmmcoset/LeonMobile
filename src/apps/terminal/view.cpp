#include "application.hpp"
#include <cmath>
using namespace mobile;
namespace leon::apps {
void TerminalApplication::paint_header(Canvas &c) {
    ApplicationWindow::paint_header(c);
    std::string status = screen.title();
    if (status.empty())
        status = "sh · " + std::to_string(screen.columns()) + " × " + std::to_string(screen.rows());
    if (screen.offset())
        status = tr(en, "历史记录 · ", "History · ") + std::to_string(screen.offset());
    if (started && !session.running())
        status = tr(en, "会话已结束 · 退出码 ", "Session ended · exit ") +
                 std::to_string(session.status());
    c.text(status, {24, 65, c.width - 110., 18}, 11, c.theme.muted);
}
void TerminalApplication::paint_content(Canvas &c, double bottom) {
    double w = c.width;
    terminal_bounds = {16, 88, w - 32, std::max(70., bottom - 212)};
    int columns = std::clamp(int((terminal_bounds.w - 20) / renderer.cell_width()), 8, 512);
    int rows = std::clamp(int((terminal_bounds.h - 20) / renderer.cell_height()), 2, 256);
    screen.resize(rows, columns);
    if (!started) {
        session.start(rows, columns);
        started = true;
    } else if (rows != last_rows || columns != last_columns) {
        selection.active = false;
        session.resize(rows, columns);
    }
    last_rows = rows;
    last_columns = columns;
    update_io();
    renderer.draw(c, terminal_bounds, screen, selection, app.focused(),
                  c.reduced_motion() || blink_on);
    if (screen.bells() != last_bells) {
        last_bells = screen.bells();
        c.rounded({16, 88, w - 32, 3}, c.theme.primary, 1);
    }
    if (!preedit.empty()) {
        c.rounded({24, terminal_bounds.y + terminal_bounds.h - 28, w - 48, 24}, c.theme.container,
                  6);
        c.text(preedit, {30, terminal_bounds.y + terminal_bounds.h - 28, w - 60, 24}, 14,
               c.theme.text);
    }
    double top = bottom - 112;
    double key_width = (w - 40) / 8;
    auto key = [&](int column, std::string label, VTermKey code) {
        button(c, hits, {20 + column * key_width, top, key_width - 4, 42}, label,
               [this, code] { send_key(code); });
    };
    key(0, "Esc", VTERM_KEY_ESCAPE);
    key(1, "Tab", VTERM_KEY_TAB);
    button(
        c, hits, {20 + 2 * key_width, top, key_width - 4, 42}, "Ctrl",
        [this] { control_latch = !control_latch; }, control_latch);
    button(
        c, hits, {20 + 3 * key_width, top, key_width - 4, 42}, "Alt",
        [this] { alt_latch = !alt_latch; }, alt_latch);
    key(4, "←", VTERM_KEY_LEFT);
    key(5, "↓", VTERM_KEY_DOWN);
    key(6, "↑", VTERM_KEY_UP);
    key(7, "→", VTERM_KEY_RIGHT);
    double width = (w - 40) / 6;
    auto action = [&](int column, std::string label, std::function<void()> callback,
                      bool active = false) {
        button(c, hits, {20 + column * width, bottom - 62, width - 4, 44}, label,
               std::move(callback), active);
    };
    action(
        0, tr(en, "选择", "Select"),
        [this] {
            select_mode = !select_mode;
            if (!select_mode)
                selection.active = false;
        },
        select_mode);
    action(1, tr(en, "复制", "Copy"), [this] { copy_selection(); });
    action(2, tr(en, "粘贴", "Paste"), [this] { paste_clipboard(); });
    action(3, "A−", [this] { renderer.font_size(renderer.font_size() - 1); });
    action(4, "A+", [this] { renderer.font_size(renderer.font_size() + 1); });
    if (session.running())
        action(
            5, tr(en, "键盘", "Keys"), [this] { keyboard = !keyboard; }, keyboard);
    else
        action(5, tr(en, "重开", "Restart"), [this] {
            if (watched_fd >= 0)
                app.unwatch_fd(watched_fd);
            watched_fd = -1;
            session.stop();
            screen.reset();
            selection.active = false;
            session.start(screen.rows(), screen.columns());
            update_io();
        });
}
} // namespace leon::apps
