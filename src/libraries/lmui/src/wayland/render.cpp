#include "backend.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
namespace lmui {
void Application::Impl::draw() {
    if (!configured || !dirty || frame_callback)
        return;
    auto *b = buffer();
    if (!b)
        return;
    auto *image = cairo_image_surface_create_for_data(static_cast<unsigned char *>(b->data),
                                                      CAIRO_FORMAT_ARGB32, w, h, w * 4);
    auto *cr = cairo_create(image);
    bool capture = getenv("LMUI_CAPTURE_FRAME") || getenv("LMUI_QUIT_AFTER_FRAME");
    motion.begin_frame(motion_time(), app->reduced_motion() || capture);
    Canvas canvas(cr, w, h, Theme::material(app->dark), &motion);
    if (app->transparent) {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        canvas.clear({0, 0, 0, 0});
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    } else
        canvas.clear(canvas.theme.background);
    auto age = std::chrono::duration<double>(std::chrono::steady_clock::now() - opened).count();
    double opacity = app->transparent || motion.reduced() ? 1 : ease(Easing::enter, age / .22);
    dirty = false;
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
    dirty |= opacity < 1 || motion.active();
    wl_surface_attach(surface, b->buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    // Idle transparent surfaces may not receive a callback until some other
    // surface damages the output. Never leave input waiting on an idle frame.
    if (dirty) {
        frame_callback = wl_surface_frame(surface);
        static const wl_callback_listener frame_events = {
            [](void *data, wl_callback *callback, uint32_t) {
                auto &s = *static_cast<Impl *>(data);
                wl_callback_destroy(callback);
                s.frame_callback = nullptr;
            }};
        wl_callback_add_listener(frame_callback, &frame_events, this);
    }
    wl_surface_commit(surface);
    if (pending_commit && !commit_sync) {
        synced_commit = std::move(pending_commit);
        commit_sync = wl_display_sync(display);
        static const wl_callback_listener sync_events = {
            [](void *data, wl_callback *callback, uint32_t) {
                auto &s = *static_cast<Impl *>(data);
                wl_callback_destroy(callback);
                s.commit_sync = nullptr;
                auto done = std::move(s.synced_commit);
                if (s.pending_commit)
                    s.dirty = true;
                if (done)
                    done();
            }};
        wl_callback_add_listener(commit_sync, &sync_events, this);
    }
    b->busy = true;
}
} // namespace lmui
