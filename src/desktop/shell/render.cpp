#include "shell.hpp"
#include <ctime>
using namespace mobile;
namespace leon::shell {
void Shell::paint(Canvas &c) {
    hits.clear();
    double w = c.width, h = c.height;
    auto t = c.theme;
    bool overlay = page != Page::active;
    if (overlay)
        c.clear(t.background);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char time[16];
    std::strftime(time, sizeof(time), "%H:%M", &tm);
    paint_status(c, time);
    switch (page) {
    case Page::lock:
        paint_lock(c, time);
        break;
    case Page::home:
    case Page::apps:
        paint_home(c);
        break;
    case Page::tasks:
        paint_tasks(c);
        break;
    case Page::quick:
        paint_quick(c);
        break;
    case Page::notifications:
        paint_notifications(c);
        break;
    case Page::active:
        break;
    }
    if (page != Page::lock)
        paint_navigation(c);
    if (!error.empty() && overlay)
        c.text(tr(en, "系统服务暂未就绪", "System services are starting"),
               {20, h - 200, w - 40, 44}, 13, t.muted);
    app.input_region(overlay ? std::vector<Rect>{{0, 0, w, h}}
                             : std::vector<Rect>{{0, 0, w, 42}, {0, h - 64, w, 64}});
}
} // namespace leon::shell
