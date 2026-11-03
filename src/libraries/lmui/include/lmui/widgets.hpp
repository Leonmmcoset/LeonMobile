#pragma once
#include <functional>
#include <lmui/canvas.hpp>
#include <vector>
namespace lmui {
/* Retained component tree. App state lives in the application; callbacks request
 * a fresh tree on the next frame, so no component owns borrowed state. */
enum class Kind { column, row, label, button, text_field, toggle, slider, spacer, scroll, dialog };
struct Widget {
    Kind kind{Kind::column};
    Rect bounds;
    std::string text;
    std::string motion_id;
    double extent{}, gap{12}, padding{0}, value{};
    bool checked{}, filled{true}, interacting{};
    std::vector<Widget> children;
    std::function<void()> activate;
    std::function<void(double)> change;
    void layout(Rect available);
    void draw(Canvas &canvas) const;
    void draw(Canvas &canvas, const std::string &identity) const;
    const Widget *hit(double x, double y) const;
};
Widget Column(std::vector<Widget> children, double gap = 12);
Widget Row(std::vector<Widget> children, double gap = 12);
Widget Label(std::string text, double height = 32);
Widget Button(std::string text, std::function<void()> action, bool filled = true);
Widget Toggle(std::string text, bool checked, std::function<void()> action);
Widget Slider(double value, std::function<void(double)> action);
Widget TextField(std::string text, std::function<void()> focus);

} // namespace lmui
