#include "application.hpp"
#include <leon/platform.hpp>
using namespace mobile;
namespace leon::apps {
GalleryApplication::GalleryApplication() : ApplicationWindow("org.leon.gallery", "LMUI", "LMUI") {}
void GalleryApplication::text_input(const std::string &text) { edit(demo_input, text); }
void GalleryApplication::paint_content(Canvas &c, double) {
    auto t = c.theme;
    double w = c.width;
    c.text(tr(en, "为移动端设计的原生组件", "Native components for mobile"), {24, 80, w - 48, 40},
           18, t.muted);
    auto tree =
        Column({Button(tr(en, "显示通知", "Show notification"),
                       [&] {
                           perform([&] {
                               notification_rpc(
                                   {{"op", "notify"},
                                    {"title", tr(en, "你好，LeonMobile！", "Hello, LeonMobile!")}});
                           });
                       }),
                Toggle(tr(en, "开关组件", "Switch component"), demo_switch,
                       [&] { demo_switch = !demo_switch; }),
                Label(tr(en, "滑块组件", "Slider component")),
                Slider(demo_slider, [&](double v) { demo_slider = v; }),
                TextField(demo_input.empty() ? tr(en, "点此输入文字", "Tap to type") : demo_input,
                          [&] { keyboard = true; }),
                Button(
                    tr(en, "切换深浅主题", "Toggle theme"),
                    [&] {
                        theme_override = true;
                        app.dark = !app.dark;
                    },
                    false)});
    tree.layout({24, 142, w - 48, 340});
    tree.draw(c, "gallery/components", touch_.continuous());
    for (const auto &child : tree.children) {
        auto a = child.activate;
        auto change = child.change;
        auto r = child.bounds;
        click(hits, r, [a, change, r](double x, double) {
            if (a)
                a();
            if (change)
                change(std::clamp((x - r.x) / r.w, 0.0, 1.0));
        });
        hits.back().continuous = bool(change);
        hits.back().prominent = child.kind == Kind::button && child.filled;
    }
    button(c, hits, {24, 628, w - 48, 50}, audio_token.empty() ? tr(en, "播放测试音", "Play test tone") : tr(en, "停止测试音", "Stop test tone"), [this] {perform([this] {toggle_audio();});});
    button(c, hits, {24, 690, w - 48, 50}, tr(en, "分享文本到应用", "Share text to an app"), [this] {
        perform([this] {leon::platform::Client().interact({{"action", "share"}, {"mime", "text/plain"},
            {"text", demo_input.empty() ? "Hello from LeonMobile" : demo_input}});});
    });
    c.rounded({24, 520, w - 48, 90}, t.container, 28);
    c.text("Material You · LMUI 1", {40, 536, w - 80, 56}, 22, t.primary, true, 1);
}
} // namespace leon::apps
