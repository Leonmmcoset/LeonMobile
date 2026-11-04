#include "control.hpp"
#include "shell.hpp"
#include <ctime>
using namespace mobile;
namespace leon::shell {
void Shell::paint(Canvas &c) {
    shade.advance(Shade::Clock::now(), c.reduced_motion());
    if (!shade.visible() && shade_focused && !pressed) {
        shade_focused = false;
        if (page == Page::active && !active_id.empty())
            control("focus " + active_id);
    }
    hits.clear();
    double w = c.width, h = c.height;
    auto t = c.theme;
    bool overlay = page != Page::active;
    if (page != presented_page) {
        double dx = page == Page::tasks ? 32 : 0;
        double dy = page == Page::active                                 ? 0
                    : page == Page::home && presented_page == Page::apps ? -48
                                                                         : 48;
        page_transition.start(dx, dy);
        presented_page = page;
        touch_feedback.cancel();
    }
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char time[16];
    std::strftime(time, sizeof(time), "%H:%M", &tm);
    paint_status(c, time);
    c.save();
    Rect content{0, 42, w, h - 106};
    c.clip(content);
    page_transition.begin(c);
    if (overlay)
        c.clear(t.background);
    size_t first_content_hit = hits.size();
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
    case Page::active:
        break;
    }
    page_transition.end(c);
    translate_hits(hits, first_content_hit, page_transition.offset_x(), page_transition.offset_y(),
                   content);
    c.restore();
    if (page != Page::lock)
        paint_navigation(c);
    if (shade.visible()) {
        hits.clear();
        paint_shade(c, time);
        paint_navigation(c);
        if (power_menu)
            paint_power_menu(c);
    }
    if (!error.empty() && overlay && !shade.visible())
        c.text(tr(en, "系统服务暂未就绪", "System services are starting"),
               {20, h - 200, w - 40, 44}, 13, t.muted);
    paint_permission(c);
    touch_feedback.draw(c);
    app.input_region(overlay || shade.visible() || (!permission.empty() && page != Page::lock)
                         ? std::vector<Rect>{{0, 0, w, h}}
                         : std::vector<Rect>{{0, 0, w, 42}, {0, h - 64, w, 64}});
    if (shade.animating())
        c.request_frame();
}
} // namespace leon::shell
