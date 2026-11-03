#pragma once
#include "stopwatch.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class ClockApplication final : public mobile::ApplicationWindow {
  public:
    ClockApplication();

  private:
    Stopwatch stopwatch;
    void on_tick() override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
