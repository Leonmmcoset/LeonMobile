#pragma once
#include <leon/mobile/ui.hpp>
namespace mobile {
class OnScreenKeyboard {
  public:
    void draw(Canvas &canvas, std::vector<Hit> &hits, bool &visible, bool english,
              std::function<void(std::string)> input, const std::string &enter);

  private:
    bool symbols{}, shift{};
};
} // namespace mobile
