#include "../control.hpp"
#include "../shell.hpp"
#include "shade_widgets.hpp"
using namespace mobile;
namespace leon::shell {
void Shell::paint_notifications(Canvas &c, double top, double bottom) {
    auto t = c.theme;
    double w = c.width;
    c.text(tr(en, "通知", "Notifications"), {28, top, w - 56, 32}, 14, t.primary, true);
    notification_bounds = {16, top + 44, w - 32, std::max(0.0, bottom - top - 44)};
    notification_limit = std::max(0.0, notifications.size() * 104.0 - notification_bounds.h);
    notification_scroll = std::clamp(notification_scroll, 0.0, notification_limit);
    c.save();
    c.clip(notification_bounds);
    if (notifications.empty()) {
        double center = top + 112;
        c.circle(w / 2, center, 26, t.container);
        shade_ui::icon(c, shade_ui::Icon::bell, w / 2, center, t.primary, t.container);
        c.text(notifications_available
                   ? tr(en, "暂时没有新通知", "You're all caught up")
                   : tr(en, "通知服务暂时不可用", "Notifications are temporarily unavailable"),
               {24, center + 40, w - 48, 40}, 17, t.muted, false, 1);
    }
    double y = top + 44 - notification_scroll;
    for (auto it = notifications.rbegin(); it != notifications.rend(); ++it) {
        const auto &n = *it;
        c.rounded({24, y, w - 48, 92}, t.surface, 24);
        auto id = n.at("app").get<std::string>();
        std::string name = id;
        for (const auto &m : apps)
            if (m.at("id") == id)
                name = en ? m.value("name_en", id) : m.value("name", id);
        c.circle(50, y + 27, 12, t.container);
        c.text("•", {38, y + 15, 24, 24}, 18, t.primary, true, 1);
        c.text(name, {72, y + 12, w - 112, 28}, 12, t.muted);
        c.text(n.value("redacted",false)?tr(en,"通知内容已隐藏","Notification content hidden"):n.at("title").get<std::string>(), {40, y + 44, w - 80, 36}, 16, t.text);
        Rect row{24, std::max(y, notification_bounds.y), w - 48,
                 std::min(y + 92, bottom) - std::max(y, notification_bounds.y)};
        if (row.h > 0)
            shade_click(row, [this, id] { launch(id); });
        y += 104;
    }
    c.restore();
}
void Shell::paint_lock_notifications(Canvas &c,double y) {
    if(!authentication.ok || notifications_lock_epoch.empty() || notifications_lock_epoch!=authentication.epoch) return;
    size_t count=0;
    for(auto it=notifications.rbegin();it!=notifications.rend() && count<2;++it,++count) {
        if(y+88>c.height-200) break;
        c.rounded({24,y,double(c.width-48),88},c.theme.surface,24);
        auto id=it->at("app").get<std::string>();std::string name=id;
        for(const auto &app:apps) if(app.at("id")==id) name=app.value(en?"name_en":"name",id);
        c.text(name,{40,y+8,double(c.width-80),28},13,c.theme.muted);
        c.text(it->value("redacted",false)?tr(en,"通知内容已隐藏","Notification content hidden"):it->at("title").get<std::string>(),
               {40,y+40,double(c.width-80),36},16,c.theme.text);
        y+=100;
    }
}
} // namespace leon::shell
