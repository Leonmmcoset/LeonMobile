#pragma once
#include <string>
namespace leon::apps {
struct Calculator {
    double accumulator{};
    char op{};
    bool replace{true};
    std::string display = "0";
    void press(const std::string &key);
};
} // namespace leon::apps
