#include "../control.hpp"
#include "../shell.hpp"
#include "shade_widgets.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_quick(Canvas &c, double expansion) {
    auto t = c.theme;
    double w = c.width;

    double cell = (w - 60) / 2;
    Rect network{24, 160, cell, 80}, theme{36 + cell, 160, cell, 80};
    shade_ui::tile(c, network, shade_ui::Icon::network, tr(en, "以太网", "Ethernet"),
                   connected ? tr(en, "已连接", "Connected") : tr(en, "未连接", "Disconnected"),
                   connected);
    shade_click(
        network, [this] { launch("org.leon.settings"); }, connected);
    shade_ui::tile(c, theme, shade_ui::Icon::theme, tr(en, "深色主题", "Dark theme"),
                   app.dark ? tr(en, "已开启", "On") : tr(en, "已关闭", "Off"), app.dark);
    shade_click(
        theme, [this] { set_preference("dark", !app.dark); }, app.dark);
    volume_track = {};
    if (expansion <= .01)
        return;
    c.save();
    c.clip({0, 248, w, 182 * expansion});
    bool muted = prefs.value("volume", 70) == 0;
    Rect sound{24, 252, cell, 80}, lock{36 + cell, 252, cell, 80};
    shade_ui::tile(c, sound, shade_ui::Icon::sound, tr(en, "静音", "Mute"),
                   muted ? tr(en, "已开启", "On") : tr(en, "已关闭", "Off"), muted, !muted);
    shade_ui::tile(c, lock, shade_ui::Icon::lock, tr(en, "锁屏", "Lock screen"),
                   tr(en, "立即锁定", "Lock now"), false);
    if (expansion > .9) {
        shade_click(
            sound, [this] { toggle_mute(); }, muted);
        shade_click(lock, [this] { show_page(Page::lock); });
    }
    double y = 348;
    int volume = volume_preview >= 0 ? volume_preview : prefs.value("volume", 70);
    double position = c.animate("shell/shade/volume", volume / 100.0,
                                volume_gesture ? MotionSpec{0} : motion::settle);
    double thumb =
        c.animate("shell/shade/volume-thumb", volume_gesture ? 13 : 10, motion::feedback);
    c.rounded({24, y, w - 48, 76}, t.container, 28);
    shade_ui::icon(c, shade_ui::Icon::sound, 52, y + 38, t.primary, t.container, volume > 0);
    c.text(tr(en, "音量", "Volume"), {78, y + 4, 100, 26}, 12, t.muted);
    c.text(std::to_string(volume) + "%", {w - 96, y + 4, 48, 26}, 12, t.muted, false, 2);
    double start = 86, length = w - 146, center = y + 46;
    c.rounded({start, center - 4, length, 8}, t.outline, 4);
    if (position > 0)
        c.rounded({start, center - 4, length * position, 8}, t.primary, 4);
    c.circle(start + length * position, center, thumb, t.primary);
    if (expansion > .9)
        volume_track = {start, center - 22, length, 44};
    c.restore();
}
} // namespace leon::shell
