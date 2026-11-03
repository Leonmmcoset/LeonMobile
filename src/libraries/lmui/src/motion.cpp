#include <algorithm>
#include <chrono>
#include <cmath>
#include <lmui/motion.hpp>

namespace lmui {
double motion_time() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static double cubic(double t, double a, double b, double c, double d) {
    double u = 1 - t;
    return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d;
}
static double bezier(double x, double ax, double ay, double bx, double by, double cx = 0,
                     double cy = 0, double dx = 1, double dy = 1) {
    double low = 0, high = 1;
    for (int i = 0; i < 24; ++i) {
        double t = (low + high) / 2;
        if (cubic(t, cx, ax, bx, dx) < x)
            low = t;
        else
            high = t;
    }
    return cubic((low + high) / 2, cy, ay, by, dy);
}
double ease(Easing easing, double fraction) {
    double x = std::clamp(fraction, 0.0, 1.0);
    if (x == 0 || x == 1)
        return x;
    switch (easing) {
    case Easing::standard:
        return bezier(x, .2, 0, 0, 1);
    case Easing::enter:
        return bezier(x, .05, .7, .1, 1);
    case Easing::exit:
        return bezier(x, .3, 0, .8, .15);
    case Easing::emphasized:
        // The two connected cubics from the Material emphasized easing path.
        if (x < 1.0 / 6)
            return bezier(x, .05, 0, .1333333333, .06, 0, 0, 1.0 / 6, .4);
        return bezier(x, .2083333333, .82, .25, 1, 1.0 / 6, .4);
    }
    return x;
}
Color mix(Color a, Color b, double fraction) {
    double t = std::clamp(fraction, 0.0, 1.0);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t};
}
void Animation::reset(double value) {
    from_ = target_ = value;
    started_ = duration_ = 0;
}
double Animation::value(double now) const {
    if (duration_ <= 0)
        return target_;
    return from_ + (target_ - from_) * ease(easing_, (now - started_) / duration_);
}
bool Animation::active(double now) const { return duration_ > 0 && now < started_ + duration_; }
double Animation::to(double target, double now, MotionSpec spec, bool reduced) {
    if (reduced || spec.duration <= 0) {
        reset(target);
        return target;
    }
    if (target != target_) {
        from_ = value(now);
        target_ = target;
        started_ = now;
        duration_ = spec.duration / 1000;
        easing_ = spec.easing;
    }
    return value(now);
}
void MotionContext::begin_frame(double now, bool reduced) {
    ++frame_;
    now_ = now;
    reduced_ = reduced;
    active_ = false;
    for (auto i = entries_.begin(); i != entries_.end();) {
        if (i->second.seen + 1 < frame_)
            i = entries_.erase(i);
        else
            ++i;
    }
}
double MotionContext::animate(const std::string &key, double target, MotionSpec spec,
                              std::optional<double> initial) {
    auto [i, inserted] = entries_.try_emplace(key);
    if (inserted)
        i->second.animation.reset(initial.value_or(target));
    i->second.seen = frame_;
    double result = i->second.animation.to(target, now_, spec, reduced_);
    active_ |= i->second.animation.active(now_);
    return result;
}
} // namespace lmui
