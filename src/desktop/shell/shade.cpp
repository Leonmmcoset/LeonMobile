#include "shade.hpp"
#include <algorithm>
#include <cmath>

namespace leon::shell {
double Shade::compact_height(double h) { return std::max(43.0, std::min(700.0, h - 96)); }
double Shade::height(double h) const {
    double compact = compact_height(h), full = std::max(compact + 1, h - 64);
    return extent_ < 1 ? 42 + (compact - 42) * extent_ : compact + (full - compact) * (extent_ - 1);
}
double Shade::expansion() const { return std::clamp(extent_ - 1, 0.0, 1.0); }
void Shade::show(Level level, Clock::time_point now) {
    advance(now);
    target_ = static_cast<int>(level);
    from_ = extent_;
    started_ = now;
    animating_ = std::abs(target_ - from_) > .001;
    dragging_ = false;
    if (!animating_)
        extent_ = target_;
}
void Shade::reset() {
    extent_ = target_ = from_ = 0;
    animating_ = dragging_ = false;
}
void Shade::advance(Clock::time_point now) {
    if (!animating_)
        return;
    double elapsed = std::chrono::duration<double>(now - started_).count();
    double fraction = std::clamp(elapsed / .18, 0.0, 1.0);
    extent_ = from_ + (target_ - from_) * (1 - std::pow(1 - fraction, 3));
    if (fraction == 1) {
        extent_ = target_;
        animating_ = false;
    }
}
void Shade::begin(double y, double h) {
    start_y_ = y;
    start_height_ = height(h);
    start_level_ = std::clamp(int(std::lround(extent_)), 0, 2);
    animating_ = dragging_ = false;
}
void Shade::move(double y, double h) {
    if (std::abs(y - start_y_) > 8)
        dragging_ = true;
    if (!dragging_)
        return;
    double compact = compact_height(h), full = std::max(compact + 1, h - 64);
    double height = std::clamp(start_height_ + y - start_y_, 42.0, full);
    extent_ = height < compact ? (height - 42) / (compact - 42)
                               : 1 + (height - compact) / (full - compact);
    target_ = extent_;
}
bool Shade::release(double y, double h, Clock::time_point now) {
    move(y, h);
    bool dragged = dragging_;
    int level = start_level_;
    if (dragged) {
        double delta = y - start_y_;
        if (delta > 48)
            level = std::max(start_level_ + 1, int(std::lround(extent_)));
        else if (delta < -48)
            level = std::min(start_level_ - 1, int(std::lround(extent_)));
    }
    show(static_cast<Level>(std::clamp(level, 0, 2)), now);
    return dragged;
}
} // namespace leon::shell
