#include "application.hpp"
#include "widgets.hpp"
#include <ctime>
using namespace mobile;
namespace leon::apps {
namespace {
const Json &result(const Json &data) {
    static const Json empty = Json::object();
    return data.contains("result") ? data.at("result") : empty;
}
std::string timestamp(const Json &j) {
    std::time_t seconds = j.value("realtime_ms", 0LL) / 1000;
    std::tm t{};
    localtime_r(&seconds, &t);
    char text[32];
    std::strftime(text, sizeof(text), "%m-%d %H:%M:%S", &t);
    return text;
}
std::string severity(int level) {
    constexpr const char *names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARN", "NOTICE", "INFO", "DEBUG"};
    return names[std::clamp(level, 0, 7)];
}
}
double SettingsApplication::diagnostics_bottom() const {
    if (!diagnostics_detail_.is_null())
        return 600 + diagnostics_detail_.value("message", std::string{}).size() / 30 * 22;
    auto &r = result(diagnostics_data_);
    if (diagnostics_tab_ == DiagnosticsTab::logs)
        return 476 + r.value("records", Json::array()).size() * 106;
    if (diagnostics_tab_ == DiagnosticsTab::services)
        return 400 + r.value("services", Json::array()).size() * 78;
    return 480 + (r.value("reports", Json::array()).size() + r.value("exits", Json::array()).size()) * 100;
}
void SettingsApplication::paint_diagnostics(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    auto button = [&](Rect box, const std::string &label, bool selected, auto action) {
        c.rounded(box, selected ? t.container : t.surface, 18);
        c.text(label, {box.x + 8, box.y + 5, box.w - 16, box.h - 10}, 14,
               selected ? t.primary : t.text, selected, 1);
        content_hit(box, [action](double, double) { action(); });
    };
    if (!diagnostics_detail_.is_null()) {
        const auto &r = diagnostics_detail_;
        c.rounded({20, 164, w - 40, 126}, t.container, 26);
        c.text(severity(r.value("level", 6)) + " · " + timestamp(r),
               {36, 178, w - 72, 32}, 17, t.primary, true);
        c.text(r.value("source", ""), {36, 216, w - 72, 28}, 15, t.text);
        c.text("UID " + std::to_string(r.value("uid", -1)) + " · PID " +
               std::to_string(r.value("pid", -1)), {36, 252, w - 72, 24}, 13, t.muted);
        c.text(tr(en, "启动批次", "Boot ID"), {28, 310, w - 56, 24}, 13, t.primary);
        c.text(r.value("boot", ""), {28, 340, w - 56, 48}, 13, t.muted);
        c.text(r.value("message", ""), {28, 410, w - 56, diagnostics_bottom() - 410}, 15, t.text);
        return;
    }
    bool healthy = diagnostics_data_.value("storage_healthy", false);
    c.rounded({20, 164, w - 40, 116}, t.container, 26);
    c.text(diagnostics_data_.empty() ? tr(en, "正在读取诊断", "Reading diagnostics")
            : diagnostics_failed_ ? tr(en, "诊断暂不可用", "Diagnostics unavailable")
            : healthy ? tr(en, "日志服务正常", "Log service ready")
                      : tr(en, "日志存储受限", "Log storage degraded"),
           {36, 176, w - 72, 32}, 20, t.primary, true);
    auto losses = diagnostics_data_.value("disk_queue_dropped", 0LL) +
                  diagnostics_data_.value("disk_write_dropped", 0LL) +
                  diagnostics_data_.value("rate_dropped", 0LL);
    c.text(diagnostics_failed_ ? tr(en, "请解锁设备并确认系统服务可用", "Unlock the device and retry")
           : tr(en, "保留上限 4 MiB · 丢弃 ", "4 MiB retention · dropped ") + std::to_string(losses),
           {36, 214, w - 72, 26}, 13, t.muted);
    c.text(diagnostics_job_.valid() ? tr(en, "正在刷新…", "Refreshing…")
                                  : tr(en, "点击记录查看详情", "Tap a record for details"),
           {36, 246, w - 72, 22}, 12, t.muted);
    double tab_width = (w - 56) / 3;
    for (int i = 0; i < 3; ++i) {
        auto tab = static_cast<DiagnosticsTab>(i);
        std::string label = i == 0 ? tr(en, "日志", "Logs")
                            : i == 1 ? tr(en, "服务", "Services") : tr(en, "崩溃", "Crashes");
        button({20 + i * (tab_width + 8), 296, tab_width, 44}, label, diagnostics_tab_ == tab,
               [this, tab] {
                   diagnostics_clear();
                   diagnostics_tab_ = tab;
                   scroll = 0;
                   diagnostics_refresh();
               });
    }
    button({20, 352, (w - 48) / 2, 44}, tr(en, "刷新", "Refresh"), false,
           [this] { diagnostics_refresh(); });
    button({28 + (w - 48) / 2, 352, (w - 48) / 2, 44},
           diagnostics_live_ ? tr(en, "自动刷新：开", "Live: on") : tr(en, "自动刷新：关", "Live: off"),
           diagnostics_live_, [this] { diagnostics_live_ = !diagnostics_live_; });
    const auto &r = result(diagnostics_data_);
    if (diagnostics_tab_ == DiagnosticsTab::logs) {
        button({20, 408, (w - 48) / 2, 44},
               diagnostics_previous_ ? tr(en, "上次启动", "Previous boot") : tr(en, "本次启动", "Current boot"),
               diagnostics_previous_, [this] {
                   diagnostics_clear();
                   diagnostics_previous_ = !diagnostics_previous_;
                   diagnostics_refresh();
               });
        button({28 + (w - 48) / 2, 408, (w - 48) / 2, 44},
               diagnostics_warnings_ ? tr(en, "警告及错误", "Warning & error") : tr(en, "所有级别", "All levels"),
               diagnostics_warnings_, [this] {
                   diagnostics_clear();
                   diagnostics_warnings_ = !diagnostics_warnings_;
                   diagnostics_refresh();
               });
        auto records = r.value("records", Json::array());
        double y = 468;
        // Newest first on mobile; the public query remains ascending for cursors.
        for (auto it = records.rbegin(); it != records.rend(); ++it) {
            auto record = *it;
            c.rounded({20, y, w - 40, 98}, t.surface, 20);
            c.text(severity(record.value("level", 6)) + " · " + timestamp(record),
                   {34, y + 8, w - 68, 24}, 12, t.primary, true);
            c.text(record.value("source", ""), {34, y + 34, w - 68, 24}, 14, t.text, true);
            c.text(record.value("message", ""), {34, y + 62, w - 68, 26}, 12, t.muted);
            content_hit({20, y, w - 40, 98}, [this, seq = record.value("seq", 0LL)](double, double) {
                for (const auto &current : result(diagnostics_data_).value("records", Json::array()))
                    if (current.value("seq", 0LL) == seq) {
                        diagnostics_detail_ = current;
                        scroll = 0;
                        break;
                    }
            });
            y += 106;
        }
        if (records.empty() && !diagnostics_job_.valid())
            c.text(tr(en, "此筛选下没有保留的记录", "No retained records match this filter"),
                   {28, 488, w - 56, 64}, 16, t.muted);
    } else if (diagnostics_tab_ == DiagnosticsTab::services) {
        double y = 412;
        for (const auto &service : r.value("services", Json::array())) {
            c.rounded({20, y, w - 40, 70}, t.surface, 20);
            c.text(service.value("name", ""), {36, y + 6, w - 72, 28}, 17, t.text, true);
            c.text(service.value("state", "") + " · PID " + std::to_string(service.value("pid", 0)),
                   {36, y + 38, w - 72, 24}, 13, t.muted);
            y += 78;
        }
    } else {
        c.text(tr(en, "仅显示故障元数据，转储内容不会在此展开。", "Fault metadata; core contents stay private."),
               {28, 410, w - 56, 48}, 13, t.muted);
        double y = 472;
        size_t count = 0;
        for (const char *key : {"reports", "exits"}) {
            auto items = r.value(key, Json::array());
            for (auto i = items.rbegin(); i != items.rend(); ++i) {
                const auto &item = *i;
                c.rounded({20, y, w - 40, 92}, t.surface, 20);
                c.text(item.value("app", std::string("system")), {36, y + 10, w - 72, 28}, 17, t.text, true);
                c.text("PID " + std::to_string(item.value("pid", 0)) +
                       (std::string(key) == "reports" ? " · signal " + std::to_string(item.value("signal", 0))
                                                     : " · exit " + std::to_string(item.value("status", 0))),
                       {36, y + 48, w - 72, 26}, 13, t.muted);
                y += 100;
                ++count;
            }
        }
        if (!count && !diagnostics_job_.valid())
            c.text(tr(en, "暂无故障记录", "No crash records"), {28, 488, w - 56, 48}, 16, t.muted);
    }
}
} // namespace leon::apps
