#include "widgets.hpp"
#include <cmath>

namespace leon::apps::settings {
using namespace mobile;
void icon(Canvas &c, Icon type, double x, double y, Color color) {
    c.save();
    cairo_translate(c.cr, x - 12, y - 12);
    auto line = [&](double x1, double y1, double x2, double y2) {
        c.line(x1, y1, x2, y2, color, 1.8);
    };
    auto outline = [&](Rect r, double radius) {
        c.rounded(r, color, radius);
        c.rounded({r.x + 1.8, r.y + 1.8, r.w - 3.6, r.h - 3.6}, c.theme.container,
                  std::max(0.0, radius - 1.8));
    };
    switch (type) {
    case Icon::network:
        outline({2, 3, 20, 13}, 3);
        line(12, 16, 12, 21);
        line(7, 21, 17, 21);
        break;
    case Icon::display:
        c.circle(12, 12, 5, color);
        c.circle(12, 12, 3.2, c.theme.container);
        for (int i = 0; i < 8; ++i) {
            double a = i * std::acos(-1.0) / 4;
            line(12 + std::cos(a) * 8, 12 + std::sin(a) * 8, 12 + std::cos(a) * 10.5,
                 12 + std::sin(a) * 10.5);
        }
        break;
    case Icon::sound:
        line(3, 9, 7, 9);
        line(7, 9, 12, 5);
        line(12, 5, 12, 19);
        line(12, 19, 7, 15);
        line(7, 15, 3, 15);
        line(3, 15, 3, 9);
        line(16, 8, 18, 12);
        line(18, 12, 16, 16);
        line(20, 5, 23, 12);
        line(23, 12, 20, 19);
        break;
    case Icon::storage:
        outline({4, 2, 16, 20}, 4);
        line(8, 8, 16, 8);
        line(8, 12, 16, 12);
        c.circle(16, 17, 1, color);
        break;
    case Icon::system:
        line(3, 6, 21, 6);
        line(3, 12, 21, 12);
        line(3, 18, 21, 18);
        c.circle(8, 6, 3, color);
        c.circle(16, 12, 3, color);
        c.circle(10, 18, 3, color);
        break;
    case Icon::about:
        outline({5, 1, 14, 22}, 4);
        line(10, 18, 14, 18);
        break;
    case Icon::language:
        c.circle(12, 12, 10, color);
        c.circle(12, 12, 8.2, c.theme.container);
        line(3, 12, 21, 12);
        line(12, 3, 8, 12);
        line(8, 12, 12, 21);
        line(12, 3, 16, 12);
        line(16, 12, 12, 21);
        break;
    case Icon::navigation:
        c.rounded({2, 10, 20, 4}, color, 2);
        break;
    case Icon::developer:
        line(7, 5, 1, 12); line(1, 12, 7, 19);
        line(17, 5, 23, 12); line(23, 12, 17, 19);
        line(14, 2, 10, 22);
        break;
    case Icon::applications:
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 2; ++col)
                outline({2.0 + col * 12, 2.0 + row * 12, 8, 8}, 2);
        break;
    }
    c.restore();
}
void chevron(Canvas &c, double x, double y) {
    c.line(x - 3, y - 5, x + 2, y, c.theme.muted, 1.8);
    c.line(x + 2, y, x - 3, y + 5, c.theme.muted, 1.8);
}
void radio(Canvas &c, double x, double y, bool selected, const std::string &key) {
    double state = c.animate(key, selected ? 1 : 0, motion::settle);
    c.circle(x, y, 10, mix(c.theme.muted, c.theme.primary, state));
    c.circle(x, y, 8, c.theme.surface);
    if (state > 0)
        c.circle(x, y, 5 * state, c.theme.primary);
}
void toggle(Canvas &c, Rect r, bool selected, const std::string &key) {
    double state = c.animate(key, selected ? 1 : 0, motion::settle);
    c.rounded(r, mix(c.theme.outline, c.theme.primary, state), r.h / 2);
    c.circle(r.x + r.h / 2 + (r.w - r.h) * state, r.y + r.h / 2, r.h / 2 - 5 + state,
             mix(c.theme.surface, c.theme.on_primary, state));
}
void information(Canvas &c, double y, const std::string &label, const std::string &value) {
    c.text(label, {32, y, double(c.width - 64), 25}, 16, c.theme.text);
    c.text(value, {32, y + 27, double(c.width - 64), 27}, 14, c.theme.muted);
}
} // namespace leon::apps::settings
