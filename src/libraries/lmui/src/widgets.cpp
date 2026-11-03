#include <algorithm>
#include <lmui/widgets.hpp>
#include <utility>
namespace lmui {
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
void Widget::draw(Canvas &c) const { draw(c, "widget/root"); }
void Widget::draw(Canvas &c, const std::string &identity) const {
    auto t = c.theme;
    auto r = bounds;
    auto path = motion_id.empty() ? identity : "widget/" + motion_id;
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
        double state = c.animate(path + "/checked", checked ? 1 : 0, motion::settle);
        c.text(text, {r.x, r.y, r.w - 72, r.h}, 16, t.text);
        c.rounded({r.x + r.w - 60, r.y + (r.h - 32) / 2, 56, 32}, mix(t.outline, t.primary, state),
                  16);
        c.circle(r.x + r.w - 40 + 20 * state, r.y + r.h / 2, 10 + 2 * state,
                 mix(t.surface, t.on_primary, state));
    }
    if (kind == Kind::slider) {
        double position =
            c.animate(path + "/value", value, interacting ? MotionSpec{0} : motion::settle);
        c.rounded({r.x, r.y + r.h / 2 - 3, r.w, 6}, t.container, 3);
        c.rounded({r.x, r.y + r.h / 2 - 3, r.w * position, 6}, t.primary, 3);
        c.circle(r.x + r.w * position, r.y + r.h / 2, interacting ? 12 : 10, t.primary);
    }
    if (kind == Kind::dialog)
        c.rounded(r, t.surface, 28);
    c.save();
    if (kind == Kind::scroll)
        c.clip(r);
    for (size_t i = 0; i < children.size(); ++i)
        children[i].draw(c, path + "/" + std::to_string(i));
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
