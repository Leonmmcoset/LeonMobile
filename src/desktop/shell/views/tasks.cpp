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
        double position = c.animate("shell/tasks/" + id, y, motion::spatial);
        std::string name = en ? m.value("name_en", "") : m.at("name").get<std::string>();
        bool stopping = app_states.contains(id) && app_states.at(id).value("stopping", false);
        if (stopping) {
            c.rounded({24, position, w - 104, 78}, t.container, 20);
            c.text(name, {40, position + 6, w - 136, 34}, 18, t.text, true);
            c.text(tr(en, "正在关闭…", "Closing…"), {40, position + 42, w - 136, 26}, 13, t.muted);
            c.text("…", {w - 68, position, 44, 78}, 22, t.muted, false, 1);
        } else {
            button(c, hits, {24, position, w - 104, 78}, name, [this, id] { launch(id); });
            button(c, hits, {w - 68, position, 44, 78}, "×", [this, id] { dismiss_task(id); });
        }
        y += 94;
    }
    if (running.empty())
        c.text(tr(en, "暂无运行中的应用", "No running applications"), {24, 200, w - 48, 80}, 18,
               t.muted, false, 1);
}
} // namespace leon::shell
