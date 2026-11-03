#include "../control.hpp"
#include "../shell.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_lock(Canvas &c, const std::string &time) {
    auto t = c.theme;
    double w = c.width, h = c.height;
    double offset = c.animate("shell/lock/drag", pressed && !c.reduced_motion() ? lock_drag : 0,
                              pressed ? MotionSpec{0} : motion::spatial);
    c.save();
    cairo_translate(c.cr, 0, -offset);

    c.circle(w * .75, h * .45, w * .6, t.container);
    c.text(time, {24, h * .22, w - 48, 130}, 92, t.primary, false, 1);
    c.text("LeonMobile", {24, h * .22 + 140, w - 48, 50}, 24, t.text, true, 1);
    c.text(tr(en, "向上滑动解锁", "Swipe up to unlock"), {24, h - 180, w - 48, 48}, 16, t.muted,
           false, 1);
    c.restore();
}
} // namespace leon::shell
