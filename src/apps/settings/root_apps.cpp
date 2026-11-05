#include "application.hpp"
#include "system_request.hpp"
using namespace mobile;
namespace leon::apps {
void SettingsApplication::developer_run(Json operation) {
    if (root_settings_job_.valid())
        return;
    developer_message_ = tr(en, "正在处理…", "Working…");
    root_settings_job_ =
        std::async(std::launch::async, [operation = std::move(operation)]() mutable {
            return leon::root_control(std::move(operation), 10000);
        });
    app.redraw();
}
void SettingsApplication::developer_poll() {
    if (!root_settings_job_.valid() ||
        root_settings_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    auto reply = root_settings_job_.get();
    developer_message_ = reply.value("ok", false) ? tr(en, "已应用", "Applied")
                                                  : reply.value("error", "Root request failed");
    if (page_ == settings::Page::developer)
        developer_refresh();
    app.redraw();
}
void SettingsApplication::paint_root_apps(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    bool history = developer_view_ == DeveloperView::root_history;
    bool busy = root_settings_job_.valid();
    auto action = [&](Rect r, const std::string &text, std::function<void()> callback) {
        c.rounded(r, t.container, 18);
        c.text(text, r, 14, busy ? t.muted : t.primary, true, 1);
        if (!busy)
            content_hit(r, [callback = std::move(callback)](double, double) { callback(); });
    };
    action({20, 164, 96, 44}, tr(en, "上一级", "Back"), [this] { developer_back(); });
    action({w - 116, 164, 96, 44}, tr(en, "刷新", "Refresh"), [this] { developer_refresh(); });
    c.text(history ? tr(en, "Root 授权记录", "Root authorization history")
                   : tr(en, "应用 Root 请求", "Application Root requests"),
           {24, 220, w - 48, 42}, 25, t.text, true);
    c.text(history ? tr(en, "仅保存最近 64 条事件；不记录密码、参数或输出。",
                        "Keeps 64 recent events; no passwords, arguments or output.")
                   : tr(en, "每条命令都需系统弹窗与 Root 密码。未签名应用不会获得永久授权。",
                        "Every command needs system confirmation and a Root password. No permanent "
                        "grants."),
           {24, 270, w - 48, 64}, 14, t.muted);
    double y = 350;
    if (!history) {
        action({20, y, w - 40, 48},
               tr(en, "查看授权与执行记录", "Authorization and execution history"), [this] {
                   developer_view_ = DeveloperView::root_history;
                   developer_offset_ = 0;
                   scroll = 0;
                   developer_refresh();
               });
        y += 66;
        for (const auto &item : developer_data_.value("apps", Json::array())) {
            auto id = item.at("id").get<std::string>();
            bool blocked = item.value("blocked", false), available = item.value("available", false);
            c.rounded({20, y, w - 40, 186}, t.surface, 20);
            c.text(id, {32, y + 8, w - 64, 50}, 14, t.text, true);
            auto state = !available ? tr(en, "需重新安装以创建安装身份",
                                         "Reinstall to create an installation identity")
                         : blocked  ? tr(en, "已禁止申请", "Requests blocked")
                                    : tr(en, "每次询问", "Ask every time");
            c.text(item.value("version", "") + " · " + state, {32, y + 62, w - 64, 30}, 13,
                   t.muted);
            if (available) {
                action(
                    {32, y + 108, (w - 72) / 2, 48},
                    blocked ? tr(en, "允许再次询问", "Allow requests")
                            : tr(en, "禁止申请", "Block requests"),
                    [this, id, blocked] {
                        developer_auth({{"op", "app_policy"}, {"id", id}, {"blocked", !blocked}});
                    });
                action({40 + (w - 72) / 2, y + 108, (w - 72) / 2, 48},
                       tr(en, "撤销当前执行", "Revoke current work"), [this, id] {
                           developer_run({{"op", "app_revoke"}, {"id", id}});
                       });
            }
            y += 202;
        }
        if (developer_data_.value("apps", Json::array()).empty()) {
            c.text(
                tr(en, "暂无声明 root.exec 的第三方应用", "No third-party apps declare root.exec"),
                {24, y, w - 48, 60}, 16, t.muted);
            y += 76;
        }
    } else {
        for (const auto &item : developer_data_.value("events", Json::array())) {
            c.rounded({20, y, w - 40, 112}, t.surface, 18);
            c.text(item.value("id", ""), {32, y + 6, w - 64, 44}, 13, t.text, true);
            c.text(item.value("event", "") + " · status " +
                       std::to_string(item.value("status", -1)),
                   {32, y + 54, w - 64, 24}, 14, t.primary);
            c.text("v" + item.value("version", "") + " · UID " +
                       std::to_string(item.value("uid", -1)) + " · " +
                       std::to_string(item.value("time", int64_t{})),
                   {32, y + 82, w - 64, 22}, 11, t.muted);
            y += 124;
        }
        if (developer_data_.value("events", Json::array()).empty()) {
            c.text(tr(en, "暂无记录", "No history yet"), {24, y, w - 48, 48}, 16, t.muted);
            y += 64;
        }
    }
    if (developer_offset_)
        action({20, y, (w - 48) / 2, 44}, tr(en, "上一页", "Previous"), [this] {
            developer_offset_ -= 8;
            scroll = 0;
            developer_refresh();
        });
    if (developer_data_.value("more", false))
        action({28 + (w - 48) / 2, y, (w - 48) / 2, 44}, tr(en, "下一页", "Next"), [this] {
            developer_offset_ += 8;
            scroll = 0;
            developer_refresh();
        });
    c.text(developer_message_, {24, y + 58, w - 48, 76}, 13, t.muted);
}
} // namespace leon::apps
