#include "../control.hpp"
#include "../shell.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_navigation(Canvas &c) {
    auto t = c.theme;
    double w = c.width, h = c.height;

    if (prefs["navigation"] == "buttons") {
        button(c, hits, {16, h - 58, (w - 48) / 3, 48}, "‹", [this] {
            if (shade.visible())
                close_shade();
            else
                home();
        });
        button(c, hits, {24 + (w - 48) / 3, h - 58, (w - 48) / 3, 48}, "○", [this] { home(); });
        button(c, hits, {32 + 2 * (w - 48) / 3, h - 58, (w - 48) / 3, 48}, "□",
               [this] { show_page(Page::tasks); });
    } else {
        double feedback = c.animate("shell/navigation/press", pressed && down_y > h - 64 ? 1 : 0,
                                    motion::feedback);
        double length = 120 + 24 * feedback;
        c.rounded({(w - length) / 2, h - 24 - 6 * feedback, length, 5 + feedback}, t.text, 3);
        click(hits, {w / 3, h - 64, w / 3, 64}, [this] {
            if (shade.visible())
                close_shade();
            else
                home();
        });
        click(hits, {0, h - 64, w / 3, 64}, [this] { show_page(Page::tasks); });
        click(hits, {w * 2 / 3, h - 64, w / 3, 64}, [this] { show_page(Page::apps); });
    }
}
} // namespace leon::shell
