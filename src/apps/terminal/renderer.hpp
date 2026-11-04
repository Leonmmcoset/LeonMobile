#pragma once
#include "screen.hpp"
#include <leon/mobile/ui.hpp>
#include <pango/pangocairo.h>
#include <unordered_map>
namespace leon::terminal {
struct Selection {
    bool active{};
    VTermPos start{}, end{};
    bool contains(int row, int column) const;
};
class Renderer {
  public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void font_size(int pixels);
    int font_size() const { return font_size_; }
    int cell_width() const { return cell_width_; }
    int cell_height() const { return cell_height_; }
    VTermPos position(mobile::Rect bounds, double x, double y, const Screen &screen) const;
    void draw(mobile::Canvas &, mobile::Rect bounds, const Screen &, const Selection &,
              bool focused, bool blink_on);

  private:
    PangoContext *context_{};
    PangoFontDescription *font_{};
    std::unordered_map<std::string, PangoLayout *> glyphs_;
    int font_size_{}, cell_width_{}, cell_height_{}, ascent_{};
    PangoLayout *glyph(const VTermScreenCell &);
    void clear_glyphs();
};
} // namespace leon::terminal
