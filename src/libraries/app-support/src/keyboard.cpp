#include <cctype>
#include <leon/mobile/keyboard.hpp>
namespace mobile {
void OnScreenKeyboard::draw(Canvas &c, std::vector<Hit> &hits, bool &visible, bool english,
                            std::function<void(std::string)> input, const std::string &enter) {
    double w = c.width, h = c.height;
    auto t = c.theme;
    c.rounded({0, h - 246, w, 246}, t.container, 20);
    std::vector<std::string> rows =
        symbols ? std::vector<std::string>{"1234567890", "-/:;()$&@", ".,!?\"'=+"}
                : std::vector<std::string>{"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (size_t r = 0; r < rows.size(); ++r) {
        double kw = (w - 20) / rows[r].size();
        for (size_t i = 0; i < rows[r].size(); ++i) {
            std::string s(1, rows[r][i]);
            if (shift && !symbols)
                s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
            button(c, hits, {10 + i * kw, h - 236 + r * 52, kw - 4, 46}, s,
                   [input, s] { input(s); });
        }
    }
    button(c, hits, {10, h - 78, 40, 48}, "⌄", [&visible] { visible = false; });
    button(c, hits, {56, h - 78, 48, 48}, symbols ? "ABC" : "123", [this] { symbols = !symbols; });
    button(c, hits, {110, h - 78, 40, 48}, "⇧", [this] { shift = !shift; });
    button(c, hits, {156, h - 78, w - 306, 48}, tr(english, "空格", "Space"),
           [input] { input(" "); });
    button(c, hits, {w - 146, h - 78, 62, 48}, "⌫", [input] { input("\b"); });
    button(
        c, hits, {w - 78, h - 78, 68, 48}, "↵", [input, enter] { input(enter); }, true);
}
} // namespace mobile
