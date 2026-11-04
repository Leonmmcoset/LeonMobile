#include <cassert>
#include <cmath>
#include <lmui/lmui.hpp>

using namespace lmui;
int main() {
    for (auto easing : {Easing::standard, Easing::emphasized, Easing::enter, Easing::exit}) {
        assert(ease(easing, -1) == 0 && ease(easing, 2) == 1);
        double last = 0;
        for (int i = 0; i <= 1000; ++i) {
            double value = ease(easing, i / 1000.0);
            assert(value >= last && value >= 0 && value <= 1);
            last = value;
        }
    }
    assert(ease(Easing::enter, .5) > .8);
    assert(ease(Easing::exit, .5) < .3);
    assert(std::abs(ease(Easing::emphasized, 1.0 / 6) - .4) < .00001);
    Animation animation;
    assert(animation.to(1, 10) == 0);
    double halfway = animation.value(10.15);
    assert(halfway > .5 && halfway < 1);
    assert(std::abs(animation.to(0, 10.15) - halfway) < .000001);
    assert(animation.value(10.2) < halfway);
    assert(animation.value(11) == 0 && !animation.active(11));
    assert(animation.to(1, 11, motion::spatial, true) == 1);
    assert(!animation.active(11));

    MotionContext motion;
    motion.begin_frame(1);
    assert(motion.animate("toggle", 0) == 0);
    motion.begin_frame(2);
    assert(motion.animate("toggle", 1) == 0 && motion.active());
    motion.begin_frame(2.1);
    assert(motion.animate("toggle", 1) > 0);
    motion.begin_frame(3, true);
    assert(motion.animate("toggle", 0) == 0 && !motion.active());
    motion.begin_frame(4);
    motion.begin_frame(5);
    assert(motion.animate("toggle", 1) == 1); // Removed widgets do not retain stale state.

    int activations = 0;
    TouchInput input;
    std::vector<TouchTarget> targets{{{10, 10, 80, 48}, [&] { ++activations; }, {}}};
    input.handle({30, 30, PointerPhase::down}, targets);
    assert(activations == 0 && input.pressed());
    input.handle({30, 30, PointerPhase::up}, targets);
    assert(activations == 1 && !input.pressed());
    input.handle({30, 30, PointerPhase::down}, targets);
    input.handle({100, 30, PointerPhase::move}, targets);
    input.handle({30, 30, PointerPhase::up}, targets);
    assert(activations == 1); // Leaving and returning cannot accidentally click.
    input.handle({30, 30, PointerPhase::down}, targets);
    input.handle({30, 30, PointerPhase::cancel}, targets);
    input.handle({30, 30, PointerPhase::up}, targets);
    assert(activations == 1);
    input.handle({30, 30, PointerPhase::down}, targets);
    targets.clear();
    input.handle({30, 30, PointerPhase::up}, targets);
    assert(activations == 2); // Captured callback survives transient target lists.
    auto slider = Slider(.5, [&](double value) {
        assert(value >= 0 && value <= 1);
        ++activations;
    });
    slider.layout({0, 0, 100, 48});
    input.handle({10, 20, PointerPhase::down}, slider);
    input.handle({200, 20, PointerPhase::move}, slider);
    input.handle({200, 20, PointerPhase::up}, slider);
    assert(activations == 5);
    input.cancel();

    auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 128, 80);
    auto *cr = cairo_create(surface);
    Canvas canvas(cr, 128, 80, Theme::material(), &motion);
    auto pixel = [&] {
        cairo_surface_flush(surface);
        return reinterpret_cast<unsigned *>(cairo_image_surface_get_data(surface))[40 * 128 + 40];
    };
    ContentTransition transition;
    auto paint = [&](double now, Color color) {
        motion.begin_frame(now);
        canvas.clear({0, 0, 0, 1});
        transition.begin(canvas);
        canvas.rounded({0, 0, 128, 80}, color, 0);
        transition.end(canvas);
    };
    paint(10, {1, 0, 0, 1});
    assert(pixel() == 0xffff0000);
    transition.start();
    paint(11, {0, 0, 1, 1});
    assert(pixel() == 0xffff0000 && motion.active());
    paint(11.1, {0, 0, 1, 1});
    unsigned interrupted = pixel();
    assert(interrupted != 0xffff0000 && interrupted != 0xff0000ff);
    transition.start(-32);
    paint(11.1, {0, 1, 0, 1});
    assert(pixel() == interrupted); // Reversing starts from the displayed composition.
    paint(12, {0, 1, 0, 1});
    assert(pixel() == 0xff00ff00 && !motion.active());
    assert(transition.offset_x() == 0 && transition.offset_y() == 0);

    transition.start();
    paint(12.1, {1, 0, 0, 1});
    transition.reset();
    paint(12.1, {0, 0, 1, 1});
    assert(pixel() == 0xff0000ff); // Security reset cannot retain a private old page.
    assert(transition.offset_x() == 0 && transition.offset_y() == 0);

    targets = {{{10, 10, 80, 48}, [] {}, {}}};
    input.handle({30, 30, PointerPhase::down}, targets);
    input.handle({30, 30, PointerPhase::up}, targets);
    motion.begin_frame(motion_time() + .05);
    canvas.clear({1, 1, 1, 1});
    input.draw(canvas);
    assert(motion.active()); // Very short taps still receive a visible ripple.
    motion.begin_frame(motion_time() + 1);
    input.draw(canvas);
    assert(!motion.active());
    motion.begin_frame(motion_time(), true);
    input.handle({30, 30, PointerPhase::down}, targets);
    input.draw(canvas);
    assert(!motion.active()); // Reduced motion keeps immediate held feedback.
    assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
