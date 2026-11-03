#pragma once
#include "terminal.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class TerminalApplication final : public mobile::ApplicationWindow {
  public:
    TerminalApplication();

  private:
    Terminal terminal;
    void text_input(const std::string &text) override;
    bool key_event(const mobile::KeyEvent &event) override;
    std::string enter_text() const override { return "\r"; }
    void on_tick() override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
