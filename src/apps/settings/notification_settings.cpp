#include "application.hpp"
#include "widgets.hpp"
using namespace mobile;
namespace leon::apps {
void SettingsApplication::paint_notification_settings(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    if (!preferences_loaded_) {
        c.text(preferences_job_.valid()
                   ? tr(en, "正在读取通知设置…", "Loading notification settings…")
                   : tr(en, "通知服务暂不可用", "Notification service unavailable"),
               {28, 174, w - 56, 64}, 17, t.muted);
        return;
    }
    auto values = preferences_data_.at("settings");
    for (auto row : {std::pair{"sound", 164}, std::pair{"dnd", 258}}) {
        bool value = values.at(row.first);
        bool sound = std::string(row.first) == "sound";
        preference_toggle(
            c, row.second,
            sound ? tr(en, "通知提示音", "Notification sounds")
                  : tr(en, "勿扰模式", "Do not disturb"),
            sound ? tr(en, "新通知播放提示音", "Play a sound for new notifications")
                  : tr(en, "保留通知并静音", "Keep notifications and silence alerts"),
            value, [this, key = std::string(row.first), value] {
                preferences_refresh({{"op", "configure"}, {"values", {{key, !value}}}});
            });
    }
    c.text(tr(en, "锁屏通知", "Lock-screen notifications"), {28, 366, w - 56, 32}, 16, t.primary,
           true);
    double card = (w - 56) / 3;
    const char *modes[] = {"show", "hide", "none"};
    for (int i = 0; i < 3; ++i) {
        double x = 20 + i * (card + 8);
        bool chosen = values.at("lockscreen") == modes[i];
        c.rounded({x, 410, card, 70}, chosen ? t.container : t.surface, 22);
        c.text(i == 0   ? tr(en, "显示内容", "Show content")
               : i == 1 ? tr(en, "隐藏内容", "Hide content")
                        : tr(en, "不显示", "Don't show"),
               {x + 8, 422, card - 16, 44}, 14, t.text, false, 1);
        if (!preferences_mutating_)
            content_hit({x, 410, card, 70}, [this, mode = std::string(modes[i])](double, double) {
                preferences_refresh({{"op", "configure"}, {"values", {{"lockscreen", mode}}}});
            });
    }
    c.rounded({20, 500, w - 40, 52}, t.surface, 22);
    c.text(tr(en, "试听通知提示音", "Preview notification sound"), {36, 508, w - 72, 36}, 16,
           t.text);
    if (!preferences_mutating_)
        content_hit({20, 500, w - 40, 52}, [this](double, double) {
            preferences_refresh({{"op", "test_sound"}});
        });
    c.text(preferences_message_.empty() ? preferences_data_.value("audio_error", "")
                                        : preferences_message_,
           {28, 560, w - 56, 52}, 12, t.muted);
    c.text(tr(en, "按应用管理", "Manage by app"), {28, 620, w - 56, 28}, 15, t.primary, true);
    double y = 660;
    for (const auto &app : preferences_data_.value("apps", Json::array())) {
        c.rounded({20, y, w - 40, 140}, t.surface, 24);
        c.text(app.value(en ? "name_en" : "name", app.value("id", "")), {36, y + 8, w - 146, 30},
               18, t.text);
        c.text(tr(en, "允许通知", "Allow notifications"), {36, y + 44, w - 146, 26}, 13, t.muted);
        c.text(tr(en, "提示音", "Sound"), {36, y + 88, w - 146, 26}, 13, t.muted);
        bool enabled = app.value("enabled", true), sound = app.value("sound", true);
        settings::toggle(c, {w - 92, y + 32, 52, 30}, enabled,
                         "notifications/enabled/" + app.at("id").get<std::string>());
        settings::toggle(c, {w - 92, y + 86, 52, 30}, sound,
                         "notifications/sound/" + app.at("id").get<std::string>());
        if (!preferences_mutating_) {
            content_hit({20, y, w - 40, 78}, [this, app, enabled](double, double) {
                preferences_refresh(
                    {{"op", "app_policy"}, {"app", app.at("id")}, {"enabled", !enabled}});
            });
            content_hit({20, y + 78, w - 40, 62}, [this, app, sound](double, double) {
                preferences_refresh(
                    {{"op", "app_policy"}, {"app", app.at("id")}, {"sound", !sound}});
            });
        }
        y += 152;
    }
}
} // namespace leon::apps
