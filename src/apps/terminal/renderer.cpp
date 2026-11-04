#include "renderer.hpp"
#include <cmath>
namespace leon::terminal {
bool Selection::contains(int row, int column) const {
    auto first = start, last = end;
    if (vterm_pos_cmp(first, last) > 0)
        std::swap(first, last);
    VTermPos point{row, column};
    return active && vterm_pos_cmp(first, point) <= 0 && vterm_pos_cmp(point, last) <= 0;
}
Renderer::Renderer() {
    context_ = pango_font_map_create_context(pango_cairo_font_map_get_default());
    pango_context_set_base_dir(context_, PANGO_DIRECTION_LTR);
    font_ = pango_font_description_new();
    pango_font_description_set_family(font_, "DejaVu Sans Mono, monospace");
    font_size(14);
}
Renderer::~Renderer() {
    clear_glyphs();
    pango_font_description_free(font_);
    g_object_unref(context_);
}
void Renderer::clear_glyphs() {
    for (auto &[key, layout] : glyphs_) {
        (void)key;
        g_object_unref(layout);
    }
    glyphs_.clear();
}
void Renderer::font_size(int pixels) {
    pixels = std::clamp(pixels, 10, 24);
    if (pixels == font_size_)
        return;
    font_size_ = pixels;
    clear_glyphs();
    pango_font_description_set_absolute_size(font_, pixels * PANGO_SCALE);
    auto *metrics = pango_context_get_metrics(context_, font_, pango_language_from_string("en"));
    cell_width_ = int(
        std::ceil(pango_font_metrics_get_approximate_digit_width(metrics) / double(PANGO_SCALE)));
    ascent_ = int(std::ceil(pango_font_metrics_get_ascent(metrics) / double(PANGO_SCALE)));
    cell_height_ =
        ascent_ + int(std::ceil(pango_font_metrics_get_descent(metrics) / double(PANGO_SCALE))) + 2;
    pango_font_metrics_unref(metrics);
}
PangoLayout *Renderer::glyph(const VTermScreenCell &cell) {
    std::string text;
    for (auto codepoint : cell.chars) {
        if (!codepoint || codepoint == UINT32_MAX)
            break;
        text += utf8(codepoint);
    }
    std::string key = char(cell.attrs.bold | (cell.attrs.italic << 1)) + text;
    if (auto entry = glyphs_.find(key); entry != glyphs_.end())
        return entry->second;
    if (glyphs_.size() >= 1024)
        clear_glyphs();
    auto *layout = pango_layout_new(context_);
    auto *font = pango_font_description_copy(font_);
    pango_font_description_set_weight(font,
                                      cell.attrs.bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(font,
                                     cell.attrs.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_auto_dir(layout, false);
    pango_layout_set_single_paragraph_mode(layout, true);
    auto *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_font_features_new("liga=0,clig=0,calt=0,kern=0"));
    pango_layout_set_attributes(layout, attributes);
    pango_attr_list_unref(attributes);
    pango_layout_set_text(layout, text.data(), int(text.size()));
    glyphs_.emplace(std::move(key), layout);
    return layout;
}
VTermPos Renderer::position(mobile::Rect bounds, double x, double y, const Screen &screen) const {
    int row = std::clamp(int((y - bounds.y - 10) / cell_height_), 0, screen.rows() - 1);
    int scale = screen.cell(row, 0).attrs.dwl ? 2 : 1;
    int column =
        std::clamp(int((x - bounds.x - 10) / (cell_width_ * scale)), 0, screen.columns() - 1);
    if (column > 0 && screen.cell(row, column).chars[0] == UINT32_MAX)
        --column;
    return {row, column};
}
void Renderer::draw(mobile::Canvas &canvas, mobile::Rect bounds, const Screen &screen,
                    const Selection &selection, bool focused, bool blink_on) {
    using mobile::Color;
    auto *cr = canvas.cr;
    canvas.rounded(bounds, Color::hex(0x19151f), 12);
    canvas.save();
    canvas.clip_rounded(bounds, 12);
    auto cursor = screen.cursor();
    auto source = [&](VTermColor color) {
        auto rgb = screen.rgb(color).rgb;
        cairo_set_source_rgb(cr, rgb.red / 255., rgb.green / 255., rgb.blue / 255.);
    };
    for (int row = 0; row < screen.rows(); ++row) {
        for (int col = 0; col < screen.columns(); ++col) {
            auto cell = screen.cell(row, col);
            if (cell.chars[0] == UINT32_MAX)
                continue;
            int scale = cell.attrs.dwl ? 2 : 1;
            double x = bounds.x + 10 + col * cell_width_ * scale;
            double y = bounds.y + 10 + row * cell_height_;
            double width = cell_width_ * scale * std::max(1, int(cell.width));
            bool cursor_here = screen.cursor_visible() && row == cursor.row && col == cursor.col;
            bool show_cursor = cursor_here && focused && (!screen.cursor_blink() || blink_on);
            bool block_cursor =
                show_cursor && screen.cursor_shape() == VTERM_PROP_CURSORSHAPE_BLOCK;
            auto foreground = cell.fg, background = cell.bg;
            if (cell.attrs.reverse ^ block_cursor ^ selection.contains(row, col))
                std::swap(foreground, background);
            source(background);
            cairo_rectangle(cr, x, y, width, cell_height_);
            cairo_fill(cr);
            bool conceal = false;
#if VTERM_VERSION_MAJOR > 0 || VTERM_VERSION_MINOR >= 3
            conceal = cell.attrs.conceal;
#endif
            if (cell.chars[0] && !conceal && (!cell.attrs.blink || blink_on)) {
                auto *layout = glyph(cell);
                int text_width;
                pango_layout_get_pixel_size(layout, &text_width, nullptr);
                double horizontal = text_width > 0 ? std::min(1., width / text_width) : 1.;
                canvas.save();
                canvas.clip({x, y, width, double(cell_height_)});
                cairo_translate(cr, x, y);
                cairo_scale(cr, horizontal, cell.attrs.dhl ? 2 : 1);
                double baseline =
                    1 + ascent_ - pango_layout_get_baseline(layout) / double(PANGO_SCALE);
                if (cell.attrs.dhl == 2)
                    baseline -= cell_height_ / 2.;
                cairo_move_to(cr, 0, baseline);
                source(foreground);
                pango_cairo_show_layout(cr, layout);
                canvas.restore();
            }
            source(foreground);
            if (cell.attrs.underline) {
                cairo_rectangle(cr, x, y + cell_height_ - 3, width, 1);
                if (cell.attrs.underline == VTERM_UNDERLINE_DOUBLE)
                    cairo_rectangle(cr, x, y + cell_height_ - 5, width, 1);
                cairo_fill(cr);
            }
            if (cell.attrs.strike) {
                cairo_rectangle(cr, x, y + cell_height_ * .55, width, 1);
                cairo_fill(cr);
            }
            if (show_cursor && !block_cursor) {
                if (screen.cursor_shape() == VTERM_PROP_CURSORSHAPE_UNDERLINE)
                    cairo_rectangle(cr, x, y + cell_height_ - 2, width, 2);
                else
                    cairo_rectangle(cr, x, y, 2, cell_height_);
                cairo_fill(cr);
            }
            if (cursor_here && !focused) {
                cairo_set_line_width(cr, 1);
                cairo_rectangle(cr, x + .5, y + .5, width - 1, cell_height_ - 1);
                cairo_stroke(cr);
            }
        }
    }
    canvas.restore();
}
} // namespace leon::terminal
