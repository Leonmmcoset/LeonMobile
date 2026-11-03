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
    void text_input(const std::string &text) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
