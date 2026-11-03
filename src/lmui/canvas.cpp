#include <algorithm>
#include <cmath>
#include <lmui/lmui.hpp>
#include <pango/pangocairo.h>
#include <utility>
namespace lmui {
Color Color::hex(unsigned v, double a) {
    return {double((v >> 16) & 255) / 255, double((v >> 8) & 255) / 255, double(v & 255) / 255, a};
}
Theme Theme::material(bool dark) {
    if (dark)
        return {Color::hex(0x15121b), Color::hex(0x211d29), Color::hex(0x30293c),
                Color::hex(0xd0bcff), Color::hex(0x381e72), Color::hex(0xeee6f4),
                Color::hex(0xc9c0d4), Color::hex(0x4a4258), true};
    return {Color::hex(0xfff8ff), Color::hex(0xfffbff), Color::hex(0xeaddff),
            Color::hex(0x6750a4), Color::hex(0xffffff), Color::hex(0x211b2b),
            Color::hex(0x6b6177), Color::hex(0xd5cadd), false};
}
Canvas::Canvas(cairo_t *ctx, int w, int h, Theme t) : cr(ctx), width(w), height(h), theme(t) {}
static void source(cairo_t *cr, Color c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }
void Canvas::clear(Color c) {
    source(cr, c);
    cairo_paint(cr);
}
void Canvas::rounded(Rect b, Color color, double r) {
    r = std::min({r, b.w / 2, b.h / 2});
    if (b.w <= 0 || b.h <= 0)
        return;
    cairo_new_sub_path(cr);
    cairo_arc(cr, b.x + b.w - r, b.y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, b.x + b.w - r, b.y + b.h - r, r, 0, M_PI / 2);
    cairo_arc(cr, b.x + r, b.y + b.h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, b.x + r, b.y + r, r, M_PI, M_PI * 1.5);
    cairo_close_path(cr);
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
Widget Column(std::vector<Widget> c, double gap) {
    Widget w;
    w.children = std::move(c);
    w.gap = gap;
    return w;
}
Widget Row(std::vector<Widget> c, double gap) {
    auto w = Column(std::move(c), gap);
    w.kind = Kind::row;
    return w;
}
Widget Label(std::string s, double h) {
    Widget w;
    w.kind = Kind::label;
    w.text = std::move(s);
    w.extent = h;
    return w;
}
Widget Button(std::string s, std::function<void()> a, bool f) {
    Widget w;
    w.kind = Kind::button;
    w.text = std::move(s);
    w.extent = 52;
    w.activate = std::move(a);
    w.filled = f;
    return w;
}
Widget Toggle(std::string s, bool checked, std::function<void()> a) {
    auto w = Button(std::move(s), std::move(a));
    w.kind = Kind::toggle;
    w.checked = checked;
    w.extent = 60;
    return w;
}
Widget Slider(double v, std::function<void(double)> a) {
    Widget w;
    w.kind = Kind::slider;
    w.value = std::clamp(v, 0.0, 1.0);
    w.change = std::move(a);
    w.extent = 48;
    return w;
}
Widget TextField(std::string s, std::function<void()> f) {
    auto w = Button(std::move(s), std::move(f));
    w.kind = Kind::text_field;
    return w;
}
void Widget::layout(Rect r) {
    bounds = r;
    if (children.empty())
        return;
    bool horizontal = kind == Kind::row;
    double available = (horizontal ? r.w : r.h) - padding * 2 - gap * (children.size() - 1);
    double fixed = 0;
    size_t flex = 0;
    for (const auto &c : children)
        if (c.extent > 0)
            fixed += c.extent;
        else
            ++flex;
    double share = flex ? std::max(0.0, (available - fixed) / flex) : 0,
           pos = horizontal ? r.x + padding : r.y + padding;
    for (auto &c : children) {
        double n = c.extent > 0 ? c.extent : share;
        c.layout(horizontal ? Rect{pos, r.y + padding, n, r.h - padding * 2}
                            : Rect{r.x + padding, pos, r.w - padding * 2, n});
        pos += n + gap;
    }
}
void Widget::draw(Canvas &c) const {
    auto t = c.theme;
    auto r = bounds;
    if (kind == Kind::button) {
        c.rounded(r, filled ? t.primary : t.container, 26);
        c.text(text, r, 16, filled ? t.on_primary : t.primary, true, 1);
    }
    if (kind == Kind::label)
        c.text(text, r, 16, t.text);
    if (kind == Kind::text_field) {
        c.rounded(r, t.container, 12);
        c.text(text, {r.x + 16, r.y, r.w - 32, r.h}, 16, t.text);
    }
    if (kind == Kind::toggle) {
        c.text(text, {r.x, r.y, r.w - 72, r.h}, 16, t.text);
        c.rounded({r.x + r.w - 60, r.y + (r.h - 32) / 2, 56, 32}, checked ? t.primary : t.outline,
                  16);
        c.circle(r.x + r.w - (checked ? 20 : 40), r.y + r.h / 2, 12,
                 checked ? t.on_primary : t.surface);
    }
    if (kind == Kind::slider) {
        c.rounded({r.x, r.y + r.h / 2 - 3, r.w, 6}, t.container, 3);
        c.rounded({r.x, r.y + r.h / 2 - 3, r.w * value, 6}, t.primary, 3);
        c.circle(r.x + r.w * value, r.y + r.h / 2, 10, t.primary);
    }
    if (kind == Kind::dialog)
        c.rounded(r, t.surface, 28);
    c.save();
    if (kind == Kind::scroll)
        c.clip(r);
    for (const auto &child : children)
        child.draw(c);
    c.restore();
}
const Widget *Widget::hit(double x, double y) const {
    if (!bounds.contains(x, y))
        return nullptr;
    for (auto i = children.rbegin(); i != children.rend(); ++i)
        if (auto w = i->hit(x, y))
            return w;
    return activate || change ? this : nullptr;
}
} // namespace lmui
