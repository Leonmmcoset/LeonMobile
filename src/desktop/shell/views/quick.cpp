#include "../control.hpp"
#include "../shell.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_quick(Canvas &c) {
    auto t = c.theme;
    double w = c.width;

    c.text(tr(en, "快捷设置", "Quick settings"), {24, 70, w - 48, 64}, 32, t.text, true);
    button(c, hits, {24, 164, w - 48, 76},
           connected ? tr(en, "以太网 · 已连接", "Ethernet · Connected")
                     : tr(en, "以太网 · 未连接", "Ethernet · Disconnected"),
           [] {});
    button(c, hits, {24, 256, w - 48, 64},
           app.dark ? tr(en, "深色模式 · 开", "Dark theme · On")
                    : tr(en, "深色模式 · 关", "Dark theme · Off"),
           [&] {
               rpc({{"op", "set"}, {"key", "dark"}, {"value", !app.dark}});
               refresh();
           });
    button(c, hits, {24, 336, w - 48, 64}, tr(en, "锁定屏幕", "Lock screen"), [&] {
        page = Page::lock;
        control("shell");
    });
    button(c, hits, {24, 416, w - 48, 64}, tr(en, "打开设置", "Open settings"),
           [&] { launch("org.leon.settings"); });
    button(c, hits, {24, 496, w - 48, 64}, tr(en, "关机", "Power off"), [&] {
        rpc({{"op", "poweroff"}});
    });
}
} // namespace leon::shell
