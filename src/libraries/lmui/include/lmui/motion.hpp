#pragma once
#include <lmui/types.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace lmui {
// Material 3 motion tokens; times are milliseconds, clocks are monotonic seconds.
enum class Easing { standard, emphasized, enter, exit };
struct MotionSpec {
    double duration{300};
    Easing easing{Easing::standard};
};
namespace motion {
inline constexpr MotionSpec feedback{100, Easing::standard};
inline constexpr MotionSpec settle{200, Easing::standard};
inline constexpr MotionSpec enter{300, Easing::enter};
inline constexpr MotionSpec exit{200, Easing::exit};
inline constexpr MotionSpec spatial{400, Easing::emphasized};
} // namespace motion
double motion_time();
double ease(Easing easing, double fraction);
Color mix(Color from, Color to, double fraction);

// Retargets from the current presentation value, including during interruption.
class Animation {
  public:
    explicit Animation(double value = 0) { reset(value); }
    void reset(double value);
    double value(double now) const;
    double to(double target, double now, MotionSpec spec = {}, bool reduced = false);
    bool active(double now) const;

  private:
    double from_{}, target_{}, started_{}, duration_{};
    Easing easing_{Easing::standard};
};

// One context per rendered surface. Stable keys keep rebuilt trees continuous.
class MotionContext {
  public:
    void begin_frame(double now, bool reduced = false);
    double animate(const std::string &key, double target, MotionSpec spec = {},
                   std::optional<double> initial = std::nullopt);
    void request_frame() { active_ = true; }
    double time() const { return now_; }
    bool reduced() const { return reduced_; }
    bool active() const { return active_; }

  private:
    struct Entry {
        Animation animation;
        unsigned long long seen{};
    };
    std::unordered_map<std::string, Entry> entries_;
    unsigned long long frame_{};
    double now_{};
    bool reduced_{}, active_{};
};
} // namespace lmui
