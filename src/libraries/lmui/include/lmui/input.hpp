#pragma once
#include <functional>
#include <lmui/canvas.hpp>
#include <vector>

namespace lmui {
struct Widget;
enum class PointerPhase { down, move, up, cancel };
struct PointerEvent {
    double x{}, y{};
    PointerPhase phase{PointerPhase::cancel};
};
struct TouchTarget {
    Rect rect;
    std::function<void()> action;
    std::function<void(double, double)> position_action;
    bool continuous{}, prominent{};
    double radius{24};
    bool feedback{true};
};
// Single-contact capture. Actions run on release; continuous controls track motion.
// Targets/callbacks are copied at press, never borrowed from a transient widget tree.
class TouchInput {
  public:
    bool handle(const PointerEvent &event, const std::vector<TouchTarget> &targets);
    bool handle(const PointerEvent &event, const Widget &tree);
    void cancel();
    void draw(Canvas &canvas) const;
    bool pressed() const { return held_; }
    bool continuous() const { return held_ && target_.continuous; }

  private:
    TouchTarget target_;
    double x_{}, y_{}, began_{}, released_{};
    bool held_{}, visible_{}, cancelled_{};
};
} // namespace lmui
