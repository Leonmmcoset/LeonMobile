#include <lmui/types.hpp>
namespace lmui {
Color Color::hex(unsigned v, double a) {
    return {double((v >> 16) & 255) / 255, double((v >> 8) & 255) / 255, double(v & 255) / 255, a};
}
Theme Theme::material(bool dark) {
    if (dark)
        return {Color::hex(0x15121b), Color::hex(0x211d29), Color::hex(0x30293c),
                Color::hex(0xd0bcff), Color::hex(0x381e72), Color::hex(0xeee6f4),
                Color::hex(0xc9c0d4), Color::hex(0x4a4258), true};
    return {Color::hex(0xfff8ff), Color::hex(0xfffbff), Color::hex(0xeaddff),
            Color::hex(0x6750a4), Color::hex(0xffffff), Color::hex(0x211b2b),
            Color::hex(0x6b6177), Color::hex(0xd5cadd), false};
}
} // namespace lmui
