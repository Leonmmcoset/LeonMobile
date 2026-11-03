#include "application.hpp"
using namespace mobile;
namespace leon::apps {
SettingsApplication::SettingsApplication()
    : ApplicationWindow("org.leon.settings", "设置", "Settings") {}
void SettingsApplication::paint_content(Canvas &c, double) {
    auto t = c.theme;
    double w = c.width;
    c.text("LeonMobile 1.0.0", {24, 82, w - 48, 34}, 20, t.primary, true);
    c.text("Linux 7.3.1 · ARM64 · musl", {24, 118, w - 48, 30}, 14, t.muted);
    auto tree = Column(
        {Toggle(tr(en, "深色模式", "Dark theme"), app.dark, [&] { set("dark", !app.dark); }),
         Button(
             tr(en, "语言：简体中文 → English", "Language: English → 简体中文"),
             [&] { set("language", en ? "zh-CN" : "en"); }, false),
         Button(
             tr(en, "切换手势 / 三键导航", "Switch gestures / buttons"),
             [&] { set("navigation", prefs["navigation"] == "gestures" ? "buttons" : "gestures"); },
             false),
         Label(tr(en, "播放音量", "Playback volume")),
         Slider(prefs.value("volume", 70) / 100.0,
                [&](double v) { set("volume", int(v * 100)); })});
    tree.layout({24, 170, w - 48, 310});
    tree.draw(c);
    for (auto &child : tree.children) {
        auto a = child.activate;
        auto change = child.change;
        auto r = child.bounds;
        click(hits, r, [a, change, r](double x, double) {
            if (a)
                a();
            if (change)
                change(std::clamp((x - r.x) / r.w, 0.0, 1.0));
        });
    }
    c.text(tr(en, "设备：QEMU ARM64 开发原型", "Device: QEMU ARM64 prototype"),
           {24, 510, w - 48, 40}, 14, t.muted);
}
} // namespace leon::apps
