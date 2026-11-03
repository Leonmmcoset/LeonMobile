#pragma once
#include <cairo.h>
#include <lmui/motion.hpp>
#include <lmui/types.hpp>
#include <string>
namespace lmui {
class Canvas {
  public:
    Canvas(cairo_t *context, int width, int height, Theme theme);
    Canvas(cairo_t *context, int width, int height, Theme theme, MotionContext *motion);
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
    void clip_rounded(Rect rect, double radius);
    double animate(const std::string &key, double target, MotionSpec spec = {},
                   std::optional<double> initial = std::nullopt);
    double frame_time() const;
    bool reduced_motion() const;
    void request_frame();
};

// Captures the last presentation for interruption-safe page changes. Hit rectangles
// for incoming content must be translated by offset_x()/offset_y() after painting.
class ContentTransition {
  public:
    ContentTransition() = default;
    ~ContentTransition();
    ContentTransition(const ContentTransition &) = delete;
    ContentTransition &operator=(const ContentTransition &) = delete;
    void start(double dx = 32, double dy = 0);
    void begin(Canvas &canvas);
    void end(Canvas &canvas);
    double offset_x() const { return dx_ * (1 - fraction_); }
    double offset_y() const { return dy_ * (1 - fraction_); }

  private:
    cairo_pattern_t *last_{}, *previous_{};
    Animation progress_{1};
    double dx_{}, dy_{}, fraction_{1};
    bool pending_{};
};

} // namespace lmui
