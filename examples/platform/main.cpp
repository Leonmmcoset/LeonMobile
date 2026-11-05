#include <iostream>
#include <leon/platform.hpp>
int main() {
    try {
        leon::platform::Client system;
        std::cout << system.settings().dump(2) << '\n';
        std::cout << system.network().dump(2) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
