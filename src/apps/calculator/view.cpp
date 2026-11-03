#include "application.hpp"
using namespace mobile;
namespace leon::apps {
CalculatorApplication::CalculatorApplication()
    : ApplicationWindow("org.leon.calculator", "计算器", "Calculator") {}
void CalculatorApplication::paint_content(Canvas &c, double bottom) {
    auto t = c.theme;
    double w = c.width;
    c.rounded({20, 88, w - 40, 112}, t.container, 28);
    c.text(calculator.display, {36, 102, w - 72, 84}, 42, t.text, false, 2);
    std::vector<std::string> keys = {"C", "±", "%", "÷", "7", "8", "9", "×", "4", "5",
                                     "6", "−", "1", "2", "3", "+", "⌫", "0", ".", "="};
    double bh = std::min(78.0, (bottom - 226) / 5), bw = (w - 64) / 4;
    for (size_t i = 0; i < keys.size(); ++i) {
        auto k = keys[i];
        button(
            c, hits, {20 + (i % 4) * (bw + 8), 218 + (i / 4) * (bh + 8), bw, bh}, k,
            [&, k] { calculator.press(k); }, k == "=");
    }
}
} // namespace leon::apps
