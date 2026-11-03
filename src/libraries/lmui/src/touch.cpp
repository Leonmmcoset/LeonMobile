#include <algorithm>
#include <cmath>
#include <lmui/input.hpp>
#include <lmui/widgets.hpp>

namespace lmui {
void TouchInput::cancel() {
    held_ = visible_ = false;
    cancelled_ = true;
    target_ = {};
}
bool TouchInput::handle(const PointerEvent &e, const std::vector<TouchTarget> &targets) {
    if (e.phase == PointerPhase::cancel) {
        cancel();
        return false;
    }
    if (e.phase == PointerPhase::down) {
        cancel();
        for (auto i = targets.rbegin(); i != targets.rend(); ++i) {
            if (!i->rect.contains(e.x, e.y))
                continue;
            target_ = *i;
            x_ = e.x;
            y_ = e.y;
            began_ = motion_time();
            held_ = visible_ = true;
            cancelled_ = false;
            if (target_.continuous && target_.position_action) {
                auto action = target_.position_action;
                action(e.x, e.y);
            }
            return true;
        }
        return false;
    }
    if (!held_)
        return false;
    if (e.phase == PointerPhase::move) {
        if (target_.continuous && target_.position_action) {
            auto action = target_.position_action;
            action(e.x, e.y);
        } else if (std::hypot(e.x - x_, e.y - y_) > 12 || !target_.rect.contains(e.x, e.y)) {
            cancelled_ = true;
            visible_ = false;
        }
        return true;
    }
    held_ = false;
    released_ = motion_time();
    // Copy before invocation: an action may cancel this capture or rebuild its tree.
    auto target = target_;
    if (target.continuous && !cancelled_) {
        if (target.position_action)
            target.position_action(e.x, e.y);
        if (target.action)
            target.action();
        return true;
    }
    if (!cancelled_ && target.rect.contains(e.x, e.y)) {
        if (target.position_action)
            target.position_action(e.x, e.y);
        else if (target.action)
            target.action();
    }
    return true;
}
bool TouchInput::handle(const PointerEvent &e, const Widget &tree) {
    std::vector<TouchTarget> targets;
    if (e.phase == PointerPhase::down) {
        if (const auto *hit = tree.hit(e.x, e.y)) {
            TouchTarget target{hit->bounds, hit->activate, {}};
            target.prominent = hit->kind == Kind::button && hit->filled;
            target.radius = hit->kind == Kind::text_field ? 12 : 26;
            if (hit->change) {
                auto change = hit->change;
                auto bounds = hit->bounds;
                target.continuous = true;
                target.position_action = [change, bounds](double x, double) {
                    change(bounds.w > 0 ? std::clamp((x - bounds.x) / bounds.w, 0.0, 1.0) : 0);
                };
            }
            targets.push_back(std::move(target));
        }
    }
    return handle(e, targets);
}
void TouchInput::draw(Canvas &c) const {
    if (!visible_)
        return;
    if (c.reduced_motion()) {
        if (held_) {
            auto ink = target_.prominent ? c.theme.on_primary : c.theme.primary;
            ink.a *= .12;
            c.rounded(target_.rect, ink, target_.radius);
        }
        return;
    }
    double now = c.frame_time();
    double expansion = ease(Easing::standard, (now - began_) / .3);
    double fade =
        held_ ? 1 : 1 - ease(Easing::standard, (now - std::max(released_, began_ + .10)) / .18);
    if (fade <= 0)
        return;
    auto r = target_.rect;
    double radius =
        std::hypot(std::max(x_ - r.x, r.x + r.w - x_), std::max(y_ - r.y, r.y + r.h - y_));
    auto ink = target_.prominent ? c.theme.on_primary : c.theme.primary;
    c.save();
    c.clip_rounded(r, target_.radius);
    ink.a *= .06 * fade;
    c.rounded(r, ink, target_.radius);
    c.circle(x_, y_, std::max(1.0, radius * expansion), ink);
    c.restore();
    if (expansion < 1 || !held_)
        c.request_frame();
}
} // namespace lmui
