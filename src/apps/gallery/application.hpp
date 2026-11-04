#pragma once
#include <leon/mobile/application.hpp>
namespace leon::apps {
class GalleryApplication final : public mobile::ApplicationWindow {
  public:
    GalleryApplication();

  private:
    bool demo_switch{};
    double demo_slider{.4};
    std::string demo_input;
    mobile::Json save_state() const override {
        return {{"switch", demo_switch}, {"slider", demo_slider}, {"input", demo_input}};
    }
    void restore_state(const mobile::Json &state) override {
        demo_switch = state.at("switch");
        demo_slider = state.at("slider");
        demo_input = state.at("input");
    }
    void text_input(const std::string &text) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
