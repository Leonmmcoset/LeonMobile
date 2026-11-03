#include <cassert>
#include <leon/mobile/keyboard.hpp>

int main() {
    using namespace mobile;
    auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 960);
    auto *context = cairo_create(surface);
    Canvas canvas(context, 480, 960, Theme::material());
    OnScreenKeyboard keyboard;
    std::vector<Hit> hits;
    bool visible = true;
    std::string text;
    auto draw = [&] {
        hits.clear();
        keyboard.draw(
            canvas, hits, visible, false, [&](std::string value) { text += value; }, "\r");
    };
    // Activate only after draw() returns: callbacks must own their input sink.
    draw();
    activate(hits, 25, 740);
    assert(text == "q");
    activate(hits, 125, 900);
    draw();
    activate(hits, 25, 740);
    assert(text == "qQ");
    activate(hits, 430, 900);
    assert(text == "qQ\r");
    activate(hits, 25, 900);
    assert(!visible);
    cairo_destroy(context);
    cairo_surface_destroy(surface);
}
