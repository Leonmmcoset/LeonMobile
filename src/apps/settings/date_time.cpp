#include "application.hpp"
#include "widgets.hpp"
#include <ctime>
using namespace mobile;
namespace leon::apps {
void SettingsApplication::paint_datetime(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    auto button_at = [&](Rect r, const std::string &label, std::function<void()> action) {
        c.rounded(r, t.surface, 22);
        c.text(label, {r.x + 16, r.y + 8, r.w - 32, r.h - 16}, 16, t.text);
        if (preferences_loaded_ && !preferences_mutating_)
            content_hit(r, [action](double, double) { action(); });
    };
    if (!preferences_loaded_) {
        c.text(preferences_job_.valid() ? tr(en, "正在读取时间设置…", "Loading time settings…")
                                        : tr(en, "时间服务暂不可用", "Time service unavailable"),
               {28, 174, w - 56, 64}, 17, t.muted);
        return;
    }
    if (timezone_select_) {
        c.rounded({20, 164, w - 40, 60}, t.container, 24);
        c.text(timezone_query_.empty()
                   ? tr(en, "搜索时区，例如 Asia/Shanghai", "Search zones, e.g. Asia/Shanghai")
                   : timezone_query_,
               {36, 174, w - 72, 40}, 15, t.text);
        content_hit({20, 164, w - 40, 60}, [this](double, double) {
            time_field_ = 2;
            keyboard = true;
        });
        double y = 240;
        for (const auto &id : visible_timezones()) {
            button_at({20, y, w - 40, 56}, id, [this, id] {
                preferences_refresh({{"op", "configure"}, {"timezone", id}});
                timezone_select_ = false;
                time_field_ = -1;
                keyboard = false;
                scroll = 0;
            });
            y += 64;
        }
        return;
    }
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char date[32];
    strftime(date, sizeof(date), "%Y-%m-%d", &tm);
    c.rounded({20, 164, w - 40, 128}, t.container, 28);
    c.text(clock_text(tm, prefs.value("clock_24h", true), true), {40, 176, w - 80, 52}, 34,
           t.primary, true);
    c.text(date, {40, 238, w - 80, 32}, 18, t.text);
    bool automatic = preferences_data_.value("automatic", true);
    preference_toggle(
        c, 308, tr(en, "自动设置时间", "Set time automatically"),
        tr(en, "通过网络校准系统时钟", "Synchronize the system clock over the network"), automatic,
        [this, automatic] {
            preferences_refresh({{"op", "configure"}, {"automatic", !automatic}});
        });
    preference_toggle(
        c, 402, tr(en, "使用 24 小时制", "Use 24-hour format"),
        tr(en, "应用于顶栏、锁屏和时钟", "Applies to status bar, lock screen and Clock"),
        prefs.value("clock_24h", true),
        [this] { save_preference("clock_24h", !prefs.value("clock_24h", true)); });
    button_at({20, 496, w - 40, 82},
              tr(en, "时区\n", "Time zone\n") + preferences_data_.value("timezone", ""), [this] {
                  timezone_select_ = true;
                  timezone_query_.clear();
                  scroll = 0;
              });
    button_at({20, 590, w - 40, 76}, tr(en, "校时服务器 · ", "Time server · ") + time_server_,
              [this] {
                  time_field_ = 1;
                  keyboard = true;
              });
    if (time_field_ == 1 || time_server_ != preferences_data_.value("server", ""))
        button_at({20, 674, w - 40, 52}, tr(en, "保存服务器：", "Save server: ") + time_server_,
                  [this] {
                      preferences_refresh({{"op", "configure"}, {"server", time_server_}});
                      keyboard = false;
                      time_field_ = -1;
                  });
    else if (automatic)
        button_at({20, 684, w - 40, 56}, tr(en, "立即校时", "Synchronize now"), [this] {
            preferences_refresh({{"op", "sync"}});
        });
    if (!automatic) {
        button_at({20, 752, w - 40, 76}, time_manual_, [this] {
            time_field_ = 0;
            keyboard = true;
        });
        c.text(tr(en, "格式：年-月-日 时:分:秒（当前时区）",
                  "Format: YYYY-MM-DD HH:MM:SS in the current zone"),
               {28, 832, w - 56, 44}, 12, t.muted);
        button_at({20, 882, w - 40, 52}, tr(en, "应用日期与时间", "Apply date & time"), [this] {
            preferences_refresh({{"op", "set_local_time"}, {"local", time_manual_}});
            keyboard = false;
            time_field_ = -1;
        });
    }
    double y = automatic ? 764 : 958;
    auto format_stamp = [&](int64_t stamp) {
        if (!stamp)
            return tr(en, "尚无记录", "No record");
        time_t value = stamp;
        struct tm local;
        localtime_r(&value, &local);
        char text[48];
        strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
        return std::string(text);
    };
    settings::information(c, y, tr(en, "最近成功校时", "Last successful sync"),
                          format_stamp(preferences_data_.value("last_sync", int64_t{})));
    auto result = preferences_data_.value("last_result", "never");
    std::string status =
        result == "success"                            ? tr(en, "成功", "Succeeded")
        : result == "pending" || result == "scheduled" ? tr(en, "正在校时", "Synchronizing")
        : result == "error"                            ? tr(en, "失败", "Failed")
        : result == "manual"                           ? tr(en, "已手动设置", "Set manually")
        : result == "disabled" ? tr(en, "自动校时已关闭", "Automatic time disabled")
                               : tr(en, "尚未校时", "Not synchronized yet");
    c.text(status, {28, y + 84, w - 56, 30}, 15, t.primary);
    c.text(preferences_message_.empty() ? preferences_data_.value("error", "")
                                        : preferences_message_,
           {28, y + 120, w - 56, 60}, 13, t.muted);
    c.text(preferences_data_.value("rtc_error", ""), {28, y + 184, w - 56, 48}, 12, t.muted);
}
} // namespace leon::apps
