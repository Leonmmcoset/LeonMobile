#include <cassert>
#include <cstdlib>
#include <iostream>
#include <lmui/lmui.hpp>
#include <string>

// Runs against independent Weston, checking actual frame scheduling and idle behavior.
int main() {
    lmui::Application app("org.leon.motion-test", "Motion test");
    app.transparent = true;
    bool reduced = std::getenv("LMUI_REDUCE_MOTION") != nullptr;
    int phase = 0, paints = 0, idle_paints = 0, intermediate = 0;
    double last = 0, settled = 0, began = lmui::motion_time();
    bool done = false;
    app.paint = [&](lmui::Canvas &c) {
        ++paints;
        double value = c.animate("progress", phase == 0 ? 1 : 0, lmui::motion::spatial, 0);
        if (phase == 0)
            assert(value >= last);
        else
            assert(value <= last);
        if (value > 0 && value < 1)
            ++intermediate;
        c.clear(c.theme.background);
        c.rounded({24, 200, 40 + 380 * value, 64}, c.theme.primary);
        const char *evidence = std::getenv("LMUI_MOTION_EVIDENCE_DIR");
        if (evidence && phase == 0 && ((value > .2 && value < .8) || value == 1)) {
            auto path =
                std::string(evidence) + (value == 1 ? "/motion-settled.png" : "/motion-mid.png");
            assert(cairo_surface_write_to_png(cairo_get_target(c.cr), path.c_str()) ==
                   CAIRO_STATUS_SUCCESS);
        }
        last = value;
        if ((phase == 0 && value == 1) || (phase == 1 && value == 0)) {
            settled = lmui::motion_time();
            idle_paints = paints;
        }
    };
    app.tick = [&] {
        assert(lmui::motion_time() - began < 8);
        if (settled && lmui::motion_time() - settled > .35) {
            assert(paints == idle_paints);
            if (phase == 0) {
                phase = 1;
                settled = 0;
                app.redraw();
            } else {
                assert(reduced ? intermediate == 0 : intermediate >= 2);
                done = true;
                app.quit();
            }
        }
    };
    int result = app.run();
    assert(result == 0 && done);
    std::cout << "Wayland motion: " << paints << " frames, " << intermediate
              << " intermediate frames; idle and reduced motion verified\n";
}
