#include "../control.hpp"
#include "../shell.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_home(Canvas &c) {
    auto t = c.theme;
    double w = c.width, h = c.height;

    if (page == Page::home) {
        c.circle(w * .88, 220, 180, t.container);
        c.text(tr(en, "你好，Leon", "Hello, Leon"), {28, 76, w - 56, 56}, 36, t.text, true);
        c.text(tr(en, "你的移动空间", "Your mobile space"), {28, 136, w - 56, 36}, 17, t.muted);
        c.rounded({24, 210, w - 48, 124}, t.primary, 32);
        c.text("LeonMobile", {44, 228, w - 88, 48}, 30, t.on_primary, true);
        c.text(tr(en, "从这里，开始探索。", "A little space to explore."), {44, 278, w - 88, 34},
               16, t.on_primary);
    } else
        c.text(tr(en, "所有应用", "All apps"), {28, 68, w - 56, 64}, 32, t.text, true);
    double top = page == Page::home ? 378 : 160;
    size_t limit = page == Page::home ? std::min<size_t>(6, apps.size()) : apps.size();
    double cell = (w - 48) / 3;
    std::vector<unsigned> palette = {0xeaddff, 0xd1e8d5, 0xffdcc4, 0xd8e2ff,
                                     0xf4d9f2, 0xcde9e8, 0xffdf9e, 0xe2e1ee};
    for (size_t i = 0; i < limit; ++i) {
        const auto &m = apps[i];
        double x = 24 + (i % 3) * cell, y = top + (i / 3) * 118;
        std::string id = m.at("id");
        std::string name = en ? m.value("name_en", m.at("name").get<std::string>())
                              : m.at("name").get<std::string>();
        c.rounded({x + cell / 2 - 30, y, 60, 60},
                  app.dark ? t.container : Color::hex(palette[i % palette.size()]), 20);
        std::string symbol = m.value("icon", "");
        symbol = symbol == "settings"     ? "⚙"
                 : symbol == "calculator" ? "="
                 : symbol == "files"      ? "▣"
                 : symbol == "terminal"   ? ">_"
                 : symbol == "clock"      ? "◷"
                 : symbol == "editor"     ? "✎"
                 : symbol == "images"     ? "▧"
                                          : "◈";
        c.text(symbol, {x + cell / 2 - 30, y, 60, 60}, 28, t.primary, true, 1);
        c.text(name, {x, y + 66, cell, 34}, 13, t.text, false, 1);
        click(hits, {x, y, cell, 104}, [&, id] { launch(id); });
    }
    if (page == Page::home)
        button(c, hits, {48, h - 148, w - 96, 52}, tr(en, "所有应用  ↑", "All apps  ↑"),
               [&] { page = Page::apps; });
}
} // namespace leon::shell
