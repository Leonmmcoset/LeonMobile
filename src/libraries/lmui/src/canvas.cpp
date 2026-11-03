#include <algorithm>
#include <cmath>
#include <lmui/canvas.hpp>
#include <pango/pangocairo.h>
namespace lmui {
Canvas::Canvas(cairo_t *ctx, int w, int h, Theme t, MotionContext *motion)
    : cr(ctx), width(w), height(h), theme(t), motion_(motion) {}
static void source(cairo_t *cr, Color c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }
void Canvas::clear(Color c) {
    source(cr, c);
    cairo_paint(cr);
}
static void rounded_path(cairo_t *cr, Rect b, double r) {
    r = std::max(0.0, std::min({r, b.w / 2, b.h / 2}));
    cairo_new_sub_path(cr);
    cairo_arc(cr, b.x + b.w - r, b.y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, b.x + b.w - r, b.y + b.h - r, r, 0, M_PI / 2);
    cairo_arc(cr, b.x + r, b.y + b.h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, b.x + r, b.y + r, r, M_PI, M_PI * 1.5);
    cairo_close_path(cr);
}
void Canvas::rounded(Rect b, Color color, double r) {
    if (b.w <= 0 || b.h <= 0)
        return;
    rounded_path(cr, b, r);
    source(cr, color);
    cairo_fill(cr);
}
void Canvas::circle(double x, double y, double r, Color c) {
    cairo_arc(cr, x, y, r, 0, 2 * M_PI);
    source(cr, c);
    cairo_fill(cr);
}
void Canvas::line(double x1, double y1, double x2, double y2, Color c, double t) {
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_set_line_width(cr, t);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    source(cr, c);
    cairo_stroke(cr);
}
void Canvas::text(const std::string &v, Rect b, double size, Color c, bool bold, int align) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_new();
    pango_font_description_set_family(font, "sans-serif");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, v.c_str(), int(v.size()));
    pango_layout_set_width(layout, std::max(1, int(b.w * PANGO_SCALE)));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(layout, std::max(1, int(b.h * PANGO_SCALE)));
    pango_layout_set_alignment(layout, align == 1   ? PANGO_ALIGN_CENTER
                                       : align == 2 ? PANGO_ALIGN_RIGHT
                                                    : PANGO_ALIGN_LEFT);
    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    cairo_move_to(cr, b.x, b.y + std::max(0.0, (b.h - ph) / 2));
    source(cr, c);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}
void Canvas::image(cairo_surface_t *s, Rect b) {
    if (!s || cairo_surface_status(s))
        return;
    double w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    if (!w || !h)
        return;
    double scale = std::min(b.w / w, b.h / h);
    save();
    cairo_translate(cr, b.x + (b.w - w * scale) / 2, b.y + (b.h - h * scale) / 2);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
    restore();
}
void Canvas::save() { cairo_save(cr); }
void Canvas::restore() { cairo_restore(cr); }
void Canvas::clip(Rect b) {
    cairo_rectangle(cr, b.x, b.y, b.w, b.h);
    cairo_clip(cr);
}
void Canvas::clip_rounded(Rect b, double r) {
    rounded_path(cr, b, r);
    cairo_clip(cr);
}
double Canvas::animate(const std::string &key, double target, MotionSpec spec,
                       std::optional<double> initial) {
    return motion_ ? motion_->animate(key, target, spec, initial) : target;
}
double Canvas::frame_time() const { return motion_ ? motion_->time() : motion_time(); }
bool Canvas::reduced_motion() const { return !motion_ || motion_->reduced(); }
void Canvas::request_frame() {
    if (motion_)
        motion_->request_frame();
}
} // namespace lmui
