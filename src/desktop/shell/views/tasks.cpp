#include "../control.hpp"
#include "../shell.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_tasks(Canvas &c) {
    auto t = c.theme;
    double w = c.width;

    c.text(tr(en, "最近任务", "Recent tasks"), {24, 70, w - 48, 64}, 32, t.text, true);
    double y = 158;
    for (const auto &m : apps) {
        std::string id = m.at("id");
        if (!running.contains(id))
            continue;
        std::string name = en ? m.value("name_en", "") : m.at("name").get<std::string>();
        button(c, hits, {24, y, w - 104, 78}, name, [&, id] {
            active_id = id;
            page = Page::active;
            control("focus " + id);
        });
        button(c, hits, {w - 68, y, 44, 78}, "×", [&, id] {
            rpc({{"op", "stop"}, {"id", id}});
            refresh();
        });
        y += 94;
    }
    if (running.empty())
        c.text(tr(en, "暂无运行中的应用", "No running applications"), {24, 200, w - 48, 80}, 18,
               t.muted, false, 1);
}
} // namespace leon::shell
