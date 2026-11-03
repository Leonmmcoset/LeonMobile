#pragma once
#include "decoder.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class ImagesApplication final : public mobile::ApplicationWindow {
  public:
    ImagesApplication();

  private:
    std::string image_path{mobile::user_home() + "/image.png"};
    Image image;
    void text_input(const std::string &text) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
