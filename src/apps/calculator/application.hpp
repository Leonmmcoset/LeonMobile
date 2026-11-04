#pragma once
#include "model.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class CalculatorApplication final : public mobile::ApplicationWindow {
  public:
    CalculatorApplication();

  private:
    Calculator calculator;
    mobile::Json save_state() const override {
        return {{"accumulator", calculator.accumulator},
                {"op", int(calculator.op)},
                {"replace", calculator.replace},
                {"display", calculator.display}};
    }
    void restore_state(const mobile::Json &state) override {
        Calculator restored;
        restored.accumulator = state.at("accumulator");
        restored.op = char(state.at("op").get<int>());
        restored.replace = state.at("replace");
        restored.display = state.at("display");
        calculator = std::move(restored);
    }
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
