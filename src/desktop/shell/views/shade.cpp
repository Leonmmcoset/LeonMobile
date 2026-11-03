#include "../control.hpp"
#include "../shell.hpp"
#include "shade_widgets.hpp"
#include <ctime>

using namespace mobile;
namespace leon::shell {
void Shell::paint_shade(Canvas &c, const std::string &time) {
    auto t = c.theme;
    double w = c.width, h = c.height, bottom = shade.height(h);
    double expansion = shade.expansion();
    c.rounded({0, 42, w, h - 106}, {0, 0, 0, .28 * std::min(1.0, shade.extent())}, 0);
    click(hits, {0, 42, w, h - 106}, [this] { close_shade(); });
    c.rounded({0, -32, w, bottom + 32}, t.background, 32);
    // Consume taps on the panel; uncovered space dismisses it.
    click(hits, {0, 0, w, bottom}, [] {});
    c.save();
    c.clip({0, 42, w, std::max(0.0, bottom - 66)});
    c.text(time, {24, 52, 176, 64}, 44, t.text);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char date[64];
    std::strftime(date, sizeof(date), en ? "%a, %b %d" : "%Y / %m / %d", &tm);
    c.text(date, {28, 112, w - 56, 28}, 14, t.muted);
    c.rounded({w - 154, 62, 130, 36}, t.container, 18);
    c.text(tr(en, "外部供电", "External power"), {w - 150, 62, 122, 36}, 12, t.text, false, 1);
    paint_quick(c, expansion);
    paint_notifications(c, 264 + 190 * expansion, bottom - 120);
    double footer = bottom - 96;
    if (bottom > 550) {
        c.rounded({w - 132, footer, 48, 48}, t.container, 24);
        shade_ui::icon(c, shade_ui::Icon::settings, w - 108, footer + 24, t.text, t.container);
        shade_click({w - 132, footer, 48, 48}, [this] { launch("org.leon.settings"); });
        c.rounded({w - 72, footer, 48, 48}, t.primary, 24);
        shade_ui::icon(c, shade_ui::Icon::power, w - 48, footer + 24, t.on_primary, t.primary);
        shade_click({w - 72, footer, 48, 48}, [this] { power_menu = true; });
        c.text("LeonMobile", {28, footer, w - 184, 48}, 13, t.muted);
        if (!error.empty())
            c.text(tr(en, "操作失败，请稍后重试", "Couldn't apply change. Try again."),
                   {24, footer - 34, w - 48, 30}, 13, t.primary);
    }
    c.restore();
    c.rounded({w / 2 - 24, bottom - 16, 48, 4}, t.outline, 2);
    click(hits, {0, bottom - 32, w, 32}, [this] {
        open_shade(shade.expansion() > .5 ? Shade::Level::compact : Shade::Level::expanded);
    });
    paint_status(c, time);
    click(hits, {0, 0, w, 42}, [this] {
        open_shade(shade.expansion() > .5 ? Shade::Level::compact : Shade::Level::expanded);
    });
}
void Shell::paint_power_menu(Canvas &c) {
    auto t = c.theme;
    double w = c.width, h = c.height, top = h / 2 - 174;
    hits.clear();
    c.rounded({0, 0, w, h}, {0, 0, 0, .42}, 0);
    click(hits, {0, 0, w, h}, [this] { power_menu = false; });
    c.rounded({24, top, w - 48, 348}, t.surface, 32);
    click(hits, {24, top, w - 48, 348}, [] {});
    c.text(tr(en, "电源", "Power"), {48, top + 20, w - 96, 50}, 28, t.text, true);
    button(
        c, hits, {44, top + 88, w - 88, 64}, tr(en, "关机", "Power off"),
        [] {
            rpc({{"op", "poweroff"}});
        },
        true);
    button(c, hits, {44, top + 168, w - 88, 64}, tr(en, "重启", "Restart"), [] {
        rpc({{"op", "reboot"}});
    });
    button(c, hits, {44, top + 252, w - 88, 56}, tr(en, "取消", "Cancel"),
           [this] { power_menu = false; });
}
} // namespace leon::shell
