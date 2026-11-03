#pragma once
#include "model.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class CalculatorApplication final : public mobile::ApplicationWindow {
  public:
    CalculatorApplication();

  private:
    Calculator calculator;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
