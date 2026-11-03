#pragma once
#include <cairo.h>
#include <lmui/types.hpp>
#include <string>
namespace lmui {
class Canvas {
  public:
    Canvas(cairo_t *context, int width, int height, Theme theme);
    cairo_t *cr;
    int width, height;
    Theme theme;
    void clear(Color color);
    void rounded(Rect rect, Color color, double radius = 24);
    void circle(double x, double y, double r, Color color);
    void line(double x1, double y1, double x2, double y2, Color color, double thickness = 2);
    void text(const std::string &value, Rect bounds, double size = 16, Color color = {},
              bool bold = false, int align = 0);
    void image(cairo_surface_t *image, Rect bounds);
    void save();
    void restore();
    void clip(Rect rect);
};

} // namespace lmui
