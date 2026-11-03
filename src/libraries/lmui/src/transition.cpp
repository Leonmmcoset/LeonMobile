#include <algorithm>
#include <lmui/canvas.hpp>

namespace lmui {
ContentTransition::~ContentTransition() {
    if (last_)
        cairo_pattern_destroy(last_);
    if (previous_)
        cairo_pattern_destroy(previous_);
}
void ContentTransition::start(double dx, double dy) {
    if (previous_)
        cairo_pattern_destroy(previous_);
    previous_ = last_ ? cairo_pattern_reference(last_) : nullptr;
    dx_ = dx;
    dy_ = dy;
    pending_ = true;
    progress_.reset(0);
}
void ContentTransition::begin(Canvas &c) {
    if (pending_) {
        progress_.to(1, c.frame_time(), motion::spatial, c.reduced_motion());
        pending_ = false;
    }
    if (c.reduced_motion() || !previous_)
        progress_.reset(1);
    fraction_ = progress_.value(c.frame_time());
    if (progress_.active(c.frame_time()))
        c.request_frame();
    cairo_push_group(c.cr);
}
void ContentTransition::end(Canvas &c) {
    auto *current = cairo_pop_group(c.cr);
    cairo_push_group(c.cr);
    if (previous_ && fraction_ < 1) {
        c.save();
        cairo_translate(c.cr, -dx_ * .25 * fraction_, -dy_ * .25 * fraction_);
        cairo_set_source(c.cr, previous_);
        cairo_paint_with_alpha(c.cr, 1 - fraction_);
        c.restore();
    }
    c.save();
    cairo_translate(c.cr, offset_x(), offset_y());
    cairo_set_source(c.cr, current);
    cairo_paint_with_alpha(c.cr, fraction_);
    c.restore();
    cairo_pattern_destroy(current);
    if (last_)
        cairo_pattern_destroy(last_);
    last_ = cairo_pop_group(c.cr);
    cairo_set_source(c.cr, last_);
    cairo_paint(c.cr);
    if (fraction_ == 1 && previous_) {
        cairo_pattern_destroy(previous_);
        previous_ = nullptr;
    }
}
} // namespace lmui
