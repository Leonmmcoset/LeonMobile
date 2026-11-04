#include "application.hpp"
#include "input.hpp"
#include <cmath>
#include <glib.h>
#include <xkbcommon/xkbcommon-keysyms.h>
using namespace mobile;
namespace leon::apps {
TerminalApplication::TerminalApplication()
    : ApplicationWindow("org.leon.terminal", "终端", "Terminal"),
      session([this](std::string_view bytes) { screen.feed(bytes); }) {
    app.on_pointer_event([this](const PointerEvent &event) { pointer_event(event); });
    app.on_pointer_motion([this](double x, double y) {
        if (screen.mouse_mode() == VTERM_PROP_MOUSE_MOVE && !contact && !select_mode &&
            !(app.key_modifiers() & key_shift) && terminal_bounds.contains(x, y)) {
            auto position = renderer.position(terminal_bounds, x, y, screen);
            screen.mouse(position.row, position.col, 0, false, modifiers());
            update_io();
        }
    });
    app.scroll = [this](double delta) {
        int lines = int(std::lround(delta / renderer.cell_height()));
        if (!lines && delta)
            lines = delta > 0 ? 1 : -1;
        if (screen.mouse_mode() && !(app.key_modifiers() & key_shift)) {
            for (int i = 0; i < std::min(10, std::abs(lines)); ++i) {
                screen.mouse(0, 0, lines > 0 ? 5 : 4, true, modifiers());
                screen.mouse(0, 0, lines > 0 ? 5 : 4, false, modifiers());
            }
            update_io();
        } else
            scroll_terminal(-lines);
    };
    app.on_text_input([this](const std::string &text) { perform([&] { text_input(text); }); },
                      [this](const std::string &text) {
                          preedit = text;
                          app.redraw();
                      },
                      [this](uint32_t before, uint32_t) {
                          if (before)
                              send_key(VTERM_KEY_BACKSPACE);
                      });
}
TerminalApplication::~TerminalApplication() {
    if (watched_fd >= 0)
        app.unwatch_fd(watched_fd);
}
unsigned TerminalApplication::modifiers() const {
    return (app.key_modifiers() & (key_shift | key_alt | key_control)) |
           (control_latch ? unsigned(key_control) : 0U) | (alt_latch ? unsigned(key_alt) : 0U);
}
void TerminalApplication::update_io() {
    if (watched_fd >= 0) {
        app.unwatch_fd(watched_fd);
        watched_fd = -1;
    }
    bool focus = app.focused();
    if (focus != last_focus) {
        last_focus = focus;
        screen.focus(focus);
    }
    bool changed = session.pump();
    auto bytes = screen.take_output();
    if (!bytes.empty() && !session.send(bytes) && session.running())
        error = session.error();
    if (session.fd() >= 0) {
        watched_fd = session.fd();
        app.watch_fd(watched_fd, fd_readable | (session.wants_write() ? unsigned(fd_writable) : 0U),
                     [this] { update_io(); });
    }
    if (!session.error().empty())
        error = session.error();
    if (screen.revision() != last_revision || changed) {
        last_revision = screen.revision();
        app.redraw();
    }
}
void TerminalApplication::on_tick() {
    update_io();
    long long phase = static_cast<long long>(motion_time() * 2);
    if (phase != blink_phase) {
        blink_phase = phase;
        blink_on = phase % 2 == 0;
        if (app.focused())
            app.redraw();
    }
}
void TerminalApplication::text_input(const std::string &text) {
    if (!g_utf8_validate(text.data(), text.size(), nullptr))
        return;
    selection.active = false;
    preedit.clear();
    if (text == "\b")
        screen.key(VTERM_KEY_BACKSPACE, modifiers());
    else if (text == "\n" || text == "\r")
        screen.key(VTERM_KEY_ENTER, modifiers());
    else if (text == "\t")
        screen.key(VTERM_KEY_TAB, modifiers());
    else {
        const char *cursor = text.data();
        while (cursor < text.data() + text.size()) {
            screen.character(g_utf8_get_char(cursor), modifiers());
            cursor = g_utf8_next_char(cursor);
        }
    }
    control_latch = alt_latch = false;
    update_io();
    app.redraw();
}
void TerminalApplication::send_key(VTermKey key) {
    selection.active = false;
    screen.key(key, modifiers());
    control_latch = alt_latch = false;
    update_io();
    app.redraw();
}
bool TerminalApplication::key_event(const KeyEvent &event) {
    auto mod = modifiers();
    if ((mod & (key_control | key_shift)) == (key_control | key_shift)) {
        if (event.symbol == XKB_KEY_C || event.symbol == XKB_KEY_c) {
            copy_selection();
            return true;
        }
        if (event.symbol == XKB_KEY_V || event.symbol == XKB_KEY_v) {
            paste_clipboard();
            return true;
        }
    }
    if ((mod & key_shift) &&
        (event.symbol == XKB_KEY_Page_Up || event.symbol == XKB_KEY_Page_Down)) {
        scroll_terminal((event.symbol == XKB_KEY_Page_Up ? 1 : -1) *
                        std::max(1, screen.rows() - 1));
        return true;
    }
    if ((mod & key_control) && (event.symbol == XKB_KEY_plus || event.symbol == XKB_KEY_equal ||
                                event.symbol == XKB_KEY_minus)) {
        renderer.font_size(renderer.font_size() + (event.symbol == XKB_KEY_minus ? -1 : 1));
        app.redraw();
        return true;
    }
    if (!terminal::key_input(screen, event.symbol, mod))
        return true;
    selection.active = false;
    control_latch = alt_latch = false;
    update_io();
    app.redraw();
    return true;
}
void TerminalApplication::copy_selection() {
    if (!selection.active)
        return;
    if (!app.copy_text(screen.selection(selection.start, selection.end)))
        error = tr(en, "剪贴板暂时不可用", "Clipboard unavailable");
    app.redraw();
}
void TerminalApplication::paste_clipboard() {
    app.request_clipboard([this](std::string text) {
        if (!g_utf8_validate(text.data(), text.size(), nullptr))
            return;
        selection.active = false;
        if (!screen.paste(text))
            error = tr(en, "粘贴内容过长", "Paste is too large");
        update_io();
        app.redraw();
    });
}
void TerminalApplication::scroll_terminal(int lines) {
    selection.active = false;
    screen.scroll(lines);
    app.redraw();
}
void TerminalApplication::pointer_event(const PointerEvent &event) {
    perform([&] {
        if (event.phase == PointerPhase::cancel) {
            if (mouse_contact)
                screen.mouse(0, 0, 1, false, modifiers());
            contact = mouse_contact = false;
            touch_.cancel();
            update_io();
            return;
        }
        if (event.phase == PointerPhase::down && terminal_bounds.contains(event.x, event.y)) {
            contact = true;
            moved = false;
            down_x = event.x;
            down_y = last_y = event.y;
            auto position = renderer.position(terminal_bounds, event.x, event.y, screen);
            mouse_contact =
                screen.mouse_mode() && !select_mode && !(app.key_modifiers() & key_shift);
            if (mouse_contact)
                screen.mouse(position.row, position.col, 1, true, modifiers());
            else if (select_mode || app.key_modifiers() & key_shift)
                selection = {true, position, position};
            else
                selection.active = false;
        } else if (contact) {
            auto position = renderer.position(terminal_bounds, event.x, event.y, screen);
            if (event.phase == PointerPhase::move) {
                moved |= std::hypot(event.x - down_x, event.y - down_y) > 10;
                if (mouse_contact)
                    screen.mouse(position.row, position.col, 0, false, modifiers());
                else if (selection.active)
                    selection.end = position;
                else if (moved) {
                    int lines = int((event.y - last_y) / renderer.cell_height());
                    if (lines) {
                        scroll_terminal(lines);
                        last_y += lines * renderer.cell_height();
                    }
                }
            } else if (event.phase == PointerPhase::up) {
                if (mouse_contact)
                    screen.mouse(position.row, position.col, 1, false, modifiers());
                else if (!moved && !select_mode && !(app.key_modifiers() & key_shift))
                    keyboard = true;
                contact = mouse_contact = false;
            }
        } else
            touch_.handle(event, hits);
        update_io();
        app.redraw();
    });
}
} // namespace leon::apps
