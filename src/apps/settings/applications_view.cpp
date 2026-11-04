#include "application.hpp"
#include "widgets.hpp"

using namespace mobile;
namespace leon::apps {
void SettingsApplication::paint_applications(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    bool busy = applications_job_.valid();
    auto action = [&](Rect r, const std::string &label, std::function<void()> callback,
                      bool enabled = true, bool filled = false) {
        c.rounded(r, filled && enabled ? t.primary : enabled ? t.container : t.surface, 22);
        c.text(label, r, 15, !enabled ? t.muted : filled ? t.on_primary : t.primary, true, 1);
        if (enabled)
            content_hit(r, [callback = std::move(callback)](double, double) { callback(); });
    };
    auto name = [&](const Json &item) {
        return item.value(en ? "name_en" : "name", item.at("id").get<std::string>());
    };
    auto source = [&](const Json &item) {
        return item.value("builtin", false)
                   ? tr(en, "系统镜像内置", "System image")
                   : tr(en, "本地安装包 · 未签名", "Local package · unsigned");
    };
    auto refresh = [&] {
        action({w - 104, 164, 84, 48}, tr(en, "刷新", "Refresh"), [this] {
            applications_message_.clear();
            applications_refresh();
        }, !busy);
    };
    if (!applications_confirmation_.empty()) {
        bool clear = applications_confirmation_ == "app_clear";
        c.rounded({20, 164, w - 40, 100}, t.container, 24);
        c.text(clear ? tr(en, "清除应用数据？", "Clear app data?")
                     : tr(en, "卸载此应用？", "Uninstall this app?"),
               {36, 178, w - 72, 40}, 25, t.text, true);
        c.text(name(application_), {36, 224, w - 72, 28}, 18, t.primary);
        c.text(clear
                   ? tr(en, "将停止应用，删除其私有文件、设置、登录状态和临时数据，并重置权限。",
                        "Stops the app, deletes its private files, settings, sign-ins and temporary data, and resets permissions.")
                   : tr(en, "将停止应用，卸载所有安装版本，删除其私有数据，并重置权限。",
                        "Stops the app, removes all installed versions and private data, and resets permissions."),
               {28, 286, w - 56, 108}, 17, t.text);
        c.text(clear ? tr(en, "安装文件会保留；共享文件不受影响。此操作无法撤销。",
                             "Keeps the app installed and shared files intact. This cannot be undone.")
                     : tr(en, "共享文件不受影响。此操作无法撤销。",
                             "Shared files are kept. This cannot be undone."),
               {28, 398, w - 56, 68}, 14, t.muted);
        action({20, 486, (w - 48) / 2, 52}, tr(en, "取消", "Cancel"), [this] {
            applications_confirmation_.clear();
            scroll = 0;
            app.redraw();
        });
        action({28 + (w - 48) / 2, 486, (w - 48) / 2, 52},
               clear ? tr(en, "清除数据", "Clear data") : tr(en, "卸载", "Uninstall"), [this] {
            auto operation = applications_confirmation_;
            applications_confirmation_.clear();
            applications_start(operation, application_id_);
        }, !busy, true);
        return;
    }
    if (application_id_.empty()) {
        c.rounded({20, 164, w - 136, 48}, t.container, 24);
        c.text(applications_query_.empty() ? tr(en, "搜索应用", "Search apps") : applications_query_,
               {36, 168, w - 168, 40}, 16, applications_query_.empty() ? t.muted : t.text);
        content_hit({20, 164, w - 136, 48}, [this](double, double) { keyboard = true; });
        refresh();
        auto items = visible_applications();
        c.text(busy ? tr(en, "正在读取…", "Loading…")
                    : !applications_message_.empty() ? applications_message_
                    : tr(en, "已安装 ", "Installed: ") + std::to_string(applications_.size()) +
                          tr(en, " 个应用", " apps"),
               {28, 222, w - 56, 48}, 13, applications_failed_ ? t.primary : t.muted);
        double y = 280;
        for (const auto &item : items) {
            Rect row{16, y, w - 32, 92};
            c.rounded(row, t.surface, 22);
            c.circle(48, y + 44, 21, t.container);
            settings::icon(c, settings::Icon::applications, 48, y + 44, t.primary);
            c.text(name(item), {84, y + 6, w - 148, 28}, 20, t.text);
            c.text(item.at("version").get<std::string>() + " · " +
                       (item.value("running", false) ? tr(en, "运行中", "Running")
                                                      : tr(en, "未运行", "Stopped")),
                   {84, y + 36, w - 148, 24}, 13, t.primary);
            c.text(source(item), {84, y + 62, w - 148, 22}, 12, t.muted);
            settings::chevron(c, w - 40, y + 44);
            if (!busy) {
                std::string id = item.at("id");
                content_hit(row, [this, id](double, double) { applications_open(id); });
            }
            y += 100;
        }
        if (items.empty() && !busy)
            c.text(!applications_loaded_ ? tr(en, "无法读取应用，请点击刷新", "Cannot load apps. Tap Refresh.")
                                         : tr(en, "没有匹配的应用", "No matching apps"),
                   {24, 300, w - 48, 64}, 16, t.muted, false, 1);
        return;
    }
    c.text(tr(en, "已安装的应用", "Installed app"), {28, 164, w - 148, 48}, 16, t.muted);
    refresh();
    if (application_.empty()) {
        c.text(busy ? tr(en, "正在读取应用信息…", "Loading app info…") : applications_message_,
               {28, 242, w - 56, 132}, 16, t.muted);
        return;
    }
    const auto &item = application_;
    c.rounded({20, 228, w - 40, 108}, t.container, 24);
    c.text(name(item), {36, 240, w - 72, 38}, 25, t.text, true);
    c.text(item.at("version").get<std::string>() + " · " + source(item),
           {36, 286, w - 72, 36}, 14, t.muted);
    c.text(application_id_, {28, 346, w - 56, 32}, 12, t.muted);
    const auto &actions = item.at("actions");
    double bw = (w - 56) / 3;
    action({20, 392, bw, 48}, tr(en, "停止", "Stop"),
           [this] { applications_start("app_stop", application_id_); }, !busy && actions.at("stop"));
    action({28 + bw, 392, bw, 48}, tr(en, "清除数据", "Clear data"),
           [this] { applications_confirm("app_clear"); }, !busy && actions.at("clear"));
    action({36 + 2 * bw, 392, bw, 48}, tr(en, "卸载", "Uninstall"),
           [this] { applications_confirm("app_uninstall"); }, !busy && actions.at("uninstall"));
    std::string status = item.value("running", false) ? tr(en, "运行中", "Running")
                                                     : tr(en, "未运行", "Stopped");
    if (application_id_ == "org.leon.settings")
        status += tr(en, " · 设置无法停止或清除自身", " · Settings cannot stop or reset itself");
    else if (item.value("builtin", false))
        status += tr(en, " · 系统应用不可卸载", " · System app cannot be uninstalled");
    c.text(status, {28, 450, w - 56, 44}, 13, t.muted);
    c.text(busy ? tr(en, "正在处理，请稍候…", "Working, please wait…") : applications_message_,
           {28, 498, w - 56, 52}, 13, applications_failed_ ? t.primary : t.muted);
    c.rounded({20, 566, w - 40, 214}, t.surface, 24);
    c.text(tr(en, "存储占用", "Storage used"), {36, 580, w - 72, 32}, 22, t.text, true);
    const auto &storage = item.at("storage");
    auto usage = [&](const char *key, double y, const std::string &label) {
        const auto &part = storage.at(key);
        c.text(label, {36, y, w * .53 - 36, 32}, 15, t.text);
        c.text(part.value("available", false) ? settings::format_bytes(part.at("bytes").get<uint64_t>())
                                              : tr(en, "不可用", "Unavailable"),
               {w * .53, y, w * .47 - 36, 32}, 15, t.muted, false, 2);
    };
    usage("package", 626, tr(en, "应用文件", "App files"));
    usage("data", 674, tr(en, "私有数据", "Private data"));
    usage("runtime", 722, tr(en, "运行时文件", "Runtime files"));
    c.text(item.value("builtin", false)
               ? tr(en, "按已分配空间估算；内置应用仅计主程序，共享库除外。",
                        "Allocated space estimate; builtin app files cover its executable, excluding shared libraries.")
               : tr(en, "按已分配空间估算；应用文件包含保留的历史版本。",
                        "Allocated space estimate; app files include retained versions."),
           {28, 792, w - 56, 60}, 12, t.muted);
    c.text(tr(en, "申请的权限", "Requested permissions"), {28, 862, w - 56, 36}, 22, t.text, true);
    double y = 910;
    for (const auto &permission : item.at("permissions")) {
        std::string key = permission.at("name"), state = permission.at("state");
        std::string label = key == "network" ? tr(en, "网络访问", "Network access")
                            : key == "files.import" ? tr(en, "导入文件", "Import files") : key;
        std::string value = state == "allowed" ? tr(en, "已允许", "Allowed")
                            : state == "denied" ? tr(en, "已拒绝", "Denied")
                            : state == "pending" ? tr(en, "等待确认", "Awaiting approval")
                            : state == "not-requested" ? tr(en, "尚未请求 · 未授权", "Not requested · no access")
                                                       : tr(en, "服务不可用", "Service unavailable");
        c.text(label, {32, y, w - 64, 26}, 16, t.text);
        c.text(value, {32, y + 27, w - 64, 25}, 13, t.muted);
        y += 64;
    }
    if (item.at("permissions").empty())
        c.text(tr(en, "此应用未声明受管权限", "No managed permissions declared"),
               {32, y, w - 64, 36}, 15, t.muted);
}
} // namespace leon::apps
