#pragma once
namespace lmui {
inline constexpr int api_version = 1;
struct Rect {
    double x{}, y{}, w{}, h{};
    bool contains(double px, double py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};
struct Color {
    double r{}, g{}, b{}, a{1};
    static Color hex(unsigned value, double alpha = 1);
};
struct Theme {
    Color background, surface, container, primary, on_primary, text, muted, outline;
    bool dark{};
    static Theme material(bool dark = false);
};
} // namespace lmui
