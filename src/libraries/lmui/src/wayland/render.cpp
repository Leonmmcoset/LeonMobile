#include "backend.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
namespace lmui {
void Application::Impl::draw() {
    if (!configured || !dirty)
        return;
    auto *b = buffer();
    if (!b)
        return;
    auto *image = cairo_image_surface_create_for_data(static_cast<unsigned char *>(b->data),
                                                      CAIRO_FORMAT_ARGB32, w, h, w * 4);
    auto *cr = cairo_create(image);
    Canvas canvas(cr, w, h, Theme::material(app->dark));
    if (app->transparent) {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        canvas.clear({0, 0, 0, 0});
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    } else
        canvas.clear(canvas.theme.background);
    auto age = std::chrono::duration<double>(std::chrono::steady_clock::now() - opened).count();
    double opacity = std::clamp(age / .18, 0.0, 1.0);
    if (getenv("LMUI_CAPTURE_FRAME") || getenv("LMUI_QUIT_AFTER_FRAME"))
        opacity = 1;
    if (opacity < 1)
        cairo_push_group(cr);
    if (app->paint)
        app->paint(canvas);
    if (opacity < 1) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, opacity);
    }
    cairo_destroy(cr);
    cairo_surface_flush(image);
    if (const char *capture = getenv("LMUI_CAPTURE_FRAME")) {
        cairo_status_t status = cairo_surface_write_to_png(image, capture);
        if (status != CAIRO_STATUS_SUCCESS)
            std::cerr << "LMUI capture: " << cairo_status_to_string(status) << '\n';
        unsetenv("LMUI_CAPTURE_FRAME");
    }
    if (getenv("LMUI_QUIT_AFTER_FRAME"))
        running = false;
    cairo_surface_destroy(image);
    wl_surface_attach(surface, b->buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    wl_surface_commit(surface);
    b->busy = true;
    dirty = opacity < 1;
}
} // namespace lmui
