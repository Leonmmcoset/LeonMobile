#pragma once
#include "renderer.hpp"
#include "session.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class TerminalApplication final : public mobile::ApplicationWindow {
  public:
    TerminalApplication();
    ~TerminalApplication() override;

  private:
    terminal::Screen screen;
    terminal::Session session;
    terminal::Renderer renderer;
    terminal::Selection selection;
    mobile::Rect terminal_bounds{};
    int watched_fd{-1}, last_rows{}, last_columns{};
    unsigned last_revision{}, last_bells{};
    bool control_latch{}, alt_latch{}, select_mode{}, contact{}, moved{}, mouse_contact{};
    bool last_focus{}, blink_on{true}, started{};
    double down_x{}, down_y{}, last_y{};
    long long blink_phase{};
    std::string preedit;
    void update_io();
    void pointer_event(const mobile::PointerEvent &);
    void scroll_terminal(int lines);
    void send_key(VTermKey key);
    void copy_selection();
    void paste_clipboard();
    unsigned modifiers() const;
    void text_input(const std::string &text) override;
    bool key_event(const mobile::KeyEvent &event) override;
    std::string enter_text() const override { return "\r"; }
    void on_tick() override;
    void paint_header(mobile::Canvas &canvas) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
