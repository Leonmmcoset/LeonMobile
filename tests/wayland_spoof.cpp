#include <cstdio>
#include <cstdlib>
#include <lmui/lmui.hpp>
int main() {
    const char *role = std::getenv("LEON_FORGED_ROLE");
    lmui::Application app(role && std::string(role) == "ime" ? "org.leon.ime" : "org.leon.shell",
                          "Untrusted role probe");
    bool focused = false;
    unsigned ticks = 0, keys = 0;
    app.paint = [](lmui::Canvas &c) { c.clear({1, 0, 1, 1}); };
    app.tick = [&] {
        focused |= app.focused();
        if (++ticks >= 25)
            app.quit();
    };
    app.key = [&](const lmui::KeyEvent &) { ++keys; };
    int result = app.run();
    std::printf("FORGED_ROLE focused=%d keys=%u\n", focused, keys);
    return result || focused || keys ? 1 : 0;
}
