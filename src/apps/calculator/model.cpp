#include "model.hpp"
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace leon::apps {
void Calculator::press(const std::string &s) {
    if (s == "C") {
        *this = Calculator{};
        return;
    }
    if (s == "⌫") {
        if (display.size() > 1)
            display.pop_back();
        else
            display = "0";
        return;
    }
    if (s.size() == 1 && (isdigit(s[0]) || s == ".")) {
        if (replace) {
            display = s == "." ? "0." : s;
            replace = false;
        } else if (display.size() < 16 && (s != "." || display.find('.') == std::string::npos))
            display += s;
        return;
    }
    double n = 0;
    try {
        n = std::stod(display);
    } catch (...) {
        display = "Error";
        replace = true;
        return;
    }
    if (s == "±") {
        display = std::to_string(-n);
        return;
    }
    if (s == "%")
        n /= 100;
    else if (op) {
        if (op == '+')
            n = accumulator + n;
        if (op == '-')
            n = accumulator - n;
        if (op == '*')
            n = accumulator * n;
        if (op == '/')
            n = n == 0 ? NAN : accumulator / n;
    }
    accumulator = n;
    op = s == "+" ? '+' : s == "−" ? '-' : s == "×" ? '*' : s == "÷" ? '/' : 0;
    std::ostringstream out;
    out << std::setprecision(12) << n;
    display = std::isfinite(n) ? out.str() : "Error";
    replace = true;
}
} // namespace leon::apps
