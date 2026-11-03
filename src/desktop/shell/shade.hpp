#pragma once
#include <chrono>

namespace leon::shell {
class Shade {
  public:
    using Clock = std::chrono::steady_clock;
    enum class Level { closed, compact, expanded };
    double extent() const { return extent_; }
    bool visible() const { return extent_ > 0 || target_ != 0; }
    bool animating() const { return animating_; }
    bool dragging() const { return dragging_; }
    double height(double screen_height) const;
    double expansion() const;
    void show(Level level, Clock::time_point now = Clock::now());
    void reset();
    void advance(Clock::time_point now);
    void begin(double y, double screen_height);
    void move(double y, double screen_height);
    bool release(double y, double screen_height, Clock::time_point now = Clock::now());

  private:
    double extent_{}, target_{}, from_{}, start_y_{}, start_height_{};
    int start_level_{};
    bool animating_{}, dragging_{};
    Clock::time_point started_{};
    static double compact_height(double screen_height);
};
} // namespace leon::shell
