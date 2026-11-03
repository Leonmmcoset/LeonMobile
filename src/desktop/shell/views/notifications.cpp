#include "../control.hpp"
#include "../shell.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_notifications(Canvas &c) {
    auto t = c.theme;
    double w = c.width, h = c.height;

    c.text(tr(en, "通知", "Notifications"), {24, 70, w - 48, 64}, 32, t.text, true);
    double y = 160;
    if (notifications.empty())
        c.text(tr(en, "暂时没有新通知", "You're all caught up"), {24, 220, w - 48, 80}, 20, t.muted,
               false, 1);
    for (const auto &n : notifications) {
        c.rounded({24, y, w - 48, 84}, t.container, 24);
        c.text(n.at("title"), {40, y + 10, w - 80, 40}, 16, t.text);
        c.text(n.at("app"), {40, y + 48, w - 80, 24}, 11, t.muted);
        y += 96;
        if (y > h - 160)
            break;
    }
}
} // namespace leon::shell
