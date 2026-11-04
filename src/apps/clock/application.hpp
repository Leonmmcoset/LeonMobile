#pragma once
#include "stopwatch.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class ClockApplication final : public mobile::ApplicationWindow {
  public:
    ClockApplication();

  private:
    Stopwatch stopwatch;
    mobile::Json save_state() const override { return {{"seconds", stopwatch.seconds()}}; }
    void restore_state(const mobile::Json &state) override {
        stopwatch.restore(state.at("seconds"));
    }
    void on_tick() override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
