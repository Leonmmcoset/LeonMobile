#pragma once
#include <cairo.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lmui {
inline constexpr int api_version = 1;
struct Rect {
    double x{}, y{}, w{}, h{};
    bool contains(double px, double py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
struct Color { double r{}, g{}, b{}, a{1}; static Color hex(unsigned value, double alpha = 1); };
struct Theme {
    Color background, surface, container, primary, on_primary, text, muted, outline;
    bool dark{};
    static Theme material(bool dark = false);
};
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
    void text(const std::string &value, Rect bounds, double size = 16, Color color = {}, bool bold = false, int align = 0);
    void image(cairo_surface_t *image, Rect bounds);
    void save();
    void restore();
    void clip(Rect rect);
};

/* Retained component tree. App state lives in the application; callbacks request
 * a fresh tree on the next frame, so no component owns borrowed state. */
enum class Kind { column, row, label, button, text_field, toggle, slider, spacer, scroll, dialog };
struct Widget {
    Kind kind{Kind::column};
    Rect bounds;
    std::string text;
    double extent{}, gap{12}, padding{0}, value{};
    bool checked{}, filled{true};
    std::vector<Widget> children;
    std::function<void()> activate;
    std::function<void(double)> change;
    void layout(Rect available);
    void draw(Canvas &canvas) const;
    const Widget *hit(double x, double y) const;
};
Widget Column(std::vector<Widget> children, double gap = 12);
Widget Row(std::vector<Widget> children, double gap = 12);
Widget Label(std::string text, double height = 32);
Widget Button(std::string text, std::function<void()> action, bool filled = true);
Widget Toggle(std::string text, bool checked, std::function<void()> action);
Widget Slider(double value, std::function<void(double)> action);
Widget TextField(std::string text, std::function<void()> focus);

struct KeyEvent { unsigned symbol{}; std::string text; bool pressed{}; };
class Application {
public:
    Application(std::string id, std::string title);
    ~Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    std::function<void(Canvas &)> paint;
    std::function<void(double, double, bool)> pointer;
    std::function<void(double)> scroll;
    std::function<void(const KeyEvent &)> key;
    std::function<void()> tick;
    bool dark{};
    bool transparent{};
    int run();
    void redraw();
    void quit();
    void input_region(const std::vector<Rect> &rectangles);
    int width() const;
    int height() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
