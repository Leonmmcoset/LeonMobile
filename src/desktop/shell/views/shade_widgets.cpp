#include "shade_widgets.hpp"
#include <cmath>

namespace leon::shell::shade_ui {
using namespace mobile;
void icon(Canvas &c, Icon type, double x, double y, Color color, Color background, bool enabled) {
    c.save();
    cairo_translate(c.cr, x - 12, y - 12);
    auto line = [&](double x1, double y1, double x2, double y2) {
        c.line(x1, y1, x2, y2, color, 2);
    };
    auto outline = [&](Rect r, double radius) {
        c.rounded(r, color, radius);
        c.rounded({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, background, std::max(0.0, radius - 2));
    };
    switch (type) {
    case Icon::network:
        outline({2, 3, 20, 13}, 3);
        line(12, 16, 12, 21);
        line(7, 21, 17, 21);
        break;
    case Icon::theme:
        c.circle(12, 12, 10, color);
        c.circle(16, 8, 8, background);
        break;
    case Icon::sound:
        line(3, 9, 7, 9);
        line(7, 9, 12, 5);
        line(12, 5, 12, 19);
        line(12, 19, 7, 15);
        line(7, 15, 3, 15);
        line(3, 15, 3, 9);
        if (enabled) {
            line(16, 8, 19, 12);
            line(19, 12, 16, 16);
        } else {
            line(17, 9, 23, 15);
            line(23, 9, 17, 15);
        }
        break;
    case Icon::lock:
        outline({4, 10, 16, 12}, 3);
        outline({7, 2, 10, 12}, 5);
        c.rounded({6, 12, 12, 8}, background, 1);
        line(12, 15, 12, 18);
        break;
    case Icon::settings:
        for (int i = 0; i < 8; ++i) {
            double angle = i * std::acos(-1.0) / 4;
            line(12 + std::cos(angle) * 7, 12 + std::sin(angle) * 7, 12 + std::cos(angle) * 10,
                 12 + std::sin(angle) * 10);
        }
        c.circle(12, 12, 8, color);
        c.circle(12, 12, 5, background);
        c.circle(12, 12, 2, color);
        break;
    case Icon::power:
        c.circle(12, 13, 9, color);
        c.circle(12, 13, 7, background);
        c.rounded({7, 1, 10, 9}, background, 0);
        line(12, 2, 12, 11);
        break;
    case Icon::chevron:
        line(5, 9, 12, 16);
        line(12, 16, 19, 9);
        break;
    case Icon::bell:
        outline({5, 4, 14, 15}, 7);
        c.rounded({3, 17, 18, 3}, color, 1.5);
        c.circle(12, 22, 1.5, color);
        break;
    }
    c.restore();
}
void tile(Canvas &c, Rect r, Icon type, const std::string &title, const std::string &subtitle,
          bool selected, bool enabled) {
    double state = c.animate("shell/shade/tile/" + std::to_string(int(type)), selected ? 1 : 0,
                             motion::settle);
    Color background = mix(c.theme.container, c.theme.primary, state);
    Color foreground = mix(c.theme.text, c.theme.on_primary, state);
    c.rounded(r, background, 28);
    icon(c, type, r.x + 30, r.y + r.h / 2, foreground, background, enabled);
    c.text(title, {r.x + 56, r.y + 12, r.w - 68, 28}, 16, foreground, true);
    Color secondary = foreground;
    secondary.a = .78;
    c.text(subtitle, {r.x + 56, r.y + 42, r.w - 68, 24}, 12, secondary);
}
} // namespace leon::shell::shade_ui
