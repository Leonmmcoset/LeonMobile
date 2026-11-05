#include "application.hpp"
#include "system_request.hpp"
#include "widgets.hpp"
#include "wire.h"
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
SettingsApplication::~SettingsApplication() {
    diagnostics_clear();
    clear_root_fields();
    clear_device_fields();
}
Json SettingsApplication::root_request(Json request) {
    auto reply = leon::root_control(std::move(request), 1200);
    if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Root request rejected"));
    return reply;
}
void SettingsApplication::clear_root_fields() {
    for (auto *field : {&root_password_, &root_new_password_, &root_confirmation_}) {
        root_clear(field->data(), field->size());
        field->clear();
    }
    root_dialog_ = false;
    developer_field_ = -1;
    keyboard = false;
    pending_root_ = Json::object();
}
void SettingsApplication::developer_refresh() {
    try {
        root_status_ = root_request({{"op", "status"}});
        if (developer_view_ == DeveloperView::home)
            return;
        if (developer_view_ == DeveloperView::root_apps ||
            developer_view_ == DeveloperView::root_history) {
            developer_data_ = root_request(
                {{"op", developer_view_ == DeveloperView::root_apps ? "app_list" : "app_history"},
                 {"offset", developer_offset_}});
            return;
        }
        std::string area = developer_view_ == DeveloperView::kernel     ? "kernel"
                           : developer_view_ == DeveloperView::modules  ? "modules"
                           : developer_view_ == DeveloperView::services ? "services"
                                                                        : "core";
        Json request = {{"op", "inspect"}, {"area", area}, {"offset", developer_offset_}};
        if (developer_view_ == DeveloperView::kernel)
            request["path"] = developer_path_;
        if (developer_view_ == DeveloperView::services && !developer_selection_.empty())
            request["name"] = developer_selection_;
        if (developer_view_ == DeveloperView::modules)
            request["available"] = modules_available_;
        developer_data_ = root_request(request);
        developer_value_ = developer_data_.value("value", "");
    } catch (const std::exception &e) {
        developer_message_ = e.what();
        developer_data_ = Json::object();
    }
    app.redraw();
}
void SettingsApplication::developer_back() {
    if (root_dialog_) {
        clear_root_fields();
        app.redraw();
        return;
    }
    if (keyboard) {
        keyboard = false;
        developer_field_ = -1;
        app.redraw();
        return;
    }
    if (developer_view_ == DeveloperView::kernel && !developer_path_.empty()) {
        auto separator = developer_path_.rfind('/');
        developer_path_ =
            separator == std::string::npos ? "" : developer_path_.substr(0, separator);
    } else if (developer_view_ == DeveloperView::root_history)
        developer_view_ = DeveloperView::root_apps;
    else if (developer_view_ == DeveloperView::services && !developer_selection_.empty())
        developer_selection_.clear();
    else
        developer_view_ = DeveloperView::home;
    developer_offset_ = 0;
    developer_message_.clear();
    scroll = 0;
    developer_refresh();
}
void SettingsApplication::developer_auth(Json operation) {
    if (root_settings_job_.valid())
        return;
    const auto requested = operation.value("op", "");
    if (requested != "enable" && requested != "disable" && requested != "password" &&
        !root_status_.value("enabled", false)) {
        developer_message_ = tr(en, "请先在开发者选项中启用 Root 权限",
                                "Enable root access in Developer options first");
        app.redraw();
        return;
    }
    clear_root_fields();
    for (auto *field : {&root_password_, &root_new_password_, &root_confirmation_})
        field->reserve(128);
    struct rlimit core = {0, 0};
    setrlimit(RLIMIT_CORE, &core);
    prctl(PR_SET_DUMPABLE, 0);
    const auto op = operation.value("op", "");
    root_current_required_ = root_status_.value("password_set", false);
    root_new_required_ = op == "password" || (op == "enable" && !root_current_required_);
    pending_root_ = std::move(operation);
    root_dialog_ = true;
    developer_field_ = root_current_required_ ? 0 : 1;
    keyboard = true;
    scroll = 0;
    developer_message_.clear();
    app.redraw();
}
void SettingsApplication::developer_submit() {
    if (root_new_required_ && (root_new_password_ != root_confirmation_ ||
                               root_new_password_.size() < 8 || root_new_password_.size() > 128)) {
        developer_message_ = tr(en, "两次密码须一致，长度为 8–128 字节",
                                "Passwords must match and contain 8–128 bytes");
        app.redraw();
        return;
    }
    Json operation = pending_root_;
    operation["password"] = root_current_required_ ? root_password_ : root_new_password_;
    if (pending_root_.value("op", "") == "password")
        operation["new_password"] = root_new_password_;
    clear_root_fields();
    developer_run(std::move(operation));
    app.redraw();
}
bool SettingsApplication::developer_text(const std::string &text) {
    if (!keyboard)
        return true;
    if (text == "\n" || text == "\r") {
        if (root_dialog_) {
            if (root_new_required_ && developer_field_ < 2)
                ++developer_field_;
            else
                developer_submit();
        } else {
            keyboard = false;
            developer_field_ = -1;
        }
        app.redraw();
        return true;
    }
    std::string *target = nullptr;
    if (root_dialog_)
        target = developer_field_ == 0   ? &root_password_
                 : developer_field_ == 1 ? &root_new_password_
                                         : &root_confirmation_;
    else if (developer_view_ == DeveloperView::kernel)
        target = &developer_value_;
    if (!target)
        return true;
    if (text == "\b") {
        auto previous = target->size();
        auto end = previous;
        if (end) {
            --end;
            while (end && (static_cast<unsigned char>((*target)[end]) & 0xc0) == 0x80)
                --end;
            root_clear(target->data() + end, previous - end);
            target->resize(end);
        }
    } else if (target->size() + text.size() <= (root_dialog_ ? 128U : 256U)) {
        bool control = std::any_of(text.begin(), text.end(),
                                   [](unsigned char c) { return c < 32 || c == 127; });
        if (!control)
            *target += text;
    }
    app.redraw();
    return true;
}
void SettingsApplication::paint_developer(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    double message_y = 490;
    auto action = [&](Rect r, const std::string &label, std::function<void()> callback,
                      bool active = false) {
        c.rounded(r, active ? t.primary : t.container, 20);
        c.text(label, r, 15, active ? t.on_primary : t.primary, true, 1);
        content_hit(r, [callback = std::move(callback)](double, double) { callback(); });
    };
    auto field = [&](double y, const std::string &label, const std::string &value, int index,
                     bool secret) {
        c.text(label, {28, y, w - 56, 26}, 13, t.muted);
        c.rounded({20, y + 30, w - 40, 48}, developer_field_ == index ? t.container : t.surface,
                  14);
        std::string shown;
        if (secret)
            for (size_t i = 0; i < value.size(); ++i)
                shown += "•";
        else
            shown = value;
        c.text(shown.empty() ? tr(en, "点击输入", "Tap to enter") : shown, {34, y + 30, w - 68, 48},
               16, t.text);
        content_hit({20, y + 30, w - 40, 48}, [this, index](double, double) {
            developer_field_ = index;
            keyboard = true;
        });
    };
    if (root_dialog_) {
        c.text(root_new_required_ ? tr(en, "设置 Root 密码", "Set root password")
                                  : tr(en, "验证 Root 密码", "Verify root password"),
               {24, 164, w - 48, 44}, 22, t.text, true);
        c.text(tr(en, "每次提权均需验证；sudo 始终禁用。",
                  "Each elevation requires authentication. sudo stays disabled."),
               {24, 210, w - 48, 48}, 13, t.muted);
        double y = 266;
        if (root_current_required_) {
            field(y, tr(en, "当前 Root 密码", "Current root password"), root_password_, 0, true);
            y += 94;
        }
        if (root_new_required_) {
            field(y, tr(en, "新密码（8–128 字节）", "New password (8–128 bytes)"),
                  root_new_password_, 1, true);
            y += 94;
            field(y, tr(en, "确认新密码", "Confirm new password"), root_confirmation_, 2, true);
            y += 94;
        }
        action({20, y + 6, (w - 48) / 2, 48}, tr(en, "取消", "Cancel"),
               [this] { clear_root_fields(); });
        action(
            {28 + (w - 48) / 2, y + 6, (w - 48) / 2, 48}, tr(en, "确认", "Confirm"),
            [this] { developer_submit(); }, true);
        c.text(developer_message_, {24, y + 66, w - 48, 70}, 13, t.muted);
        return;
    }
    if (developer_view_ == DeveloperView::home) {
        bool enabled = root_status_.value("enabled", false);
        c.rounded({20, 164, w - 40, 112}, t.container, 24);
        c.text(tr(en, "Root 权限", "Root access"), {36, 176, w - 148, 36}, 20, t.text, true);
        c.text(root_settings_job_.valid() ? tr(en, "正在处理，请稍候…", "Working, please wait…")
               : enabled ? tr(en, "已开启 · 使用时验证密码", "Enabled · password required")
                         : tr(en, "已关闭", "Disabled"),
               {36, 218, w - 148, 30}, 14, t.muted);
        settings::toggle(c, {w - 92, 200, 52, 30}, enabled, "developer/root");
        if (!root_settings_job_.valid())
            content_hit({20, 164, w - 40, 112}, [this, enabled](double, double) {
                developer_auth({{"op", enabled ? "disable" : "enable"}});
            });
        if (root_status_.value("password_set", false))
            action({20, 288, w - 40, 52}, tr(en, "修改 Root 密码", "Change root password"), [this] {
                developer_auth({{"op", "password"}});
            });
        else
            c.text(tr(en, "首次开启时必须设置密码", "A password is required on first enable"),
                   {28, 288, w - 56, 52}, 14, t.muted);
        struct Item {
            DeveloperView view;
            const char *zh, *english;
        };
        Item items[] = {
            {DeveloperView::kernel, "运行时内核参数", "Runtime kernel parameters"},
            {DeveloperView::modules, "内核模块", "Kernel modules"},
            {DeveloperView::services, "服务配置与控制", "Service configuration and control"},
            {DeveloperView::core, "核心组件状态", "Core component status"},
            {DeveloperView::root_apps, "应用 Root 请求与记录",
             "Application Root requests and history"}};
        double y = 356;
        for (auto item : items) {
            action({20, y, w - 40, 66}, tr(en, item.zh, item.english), [this, item] {
                developer_view_ = item.view;
                developer_offset_ = 0;
                developer_path_.clear();
                developer_selection_.clear();
                developer_message_.clear();
                scroll = 0;
                developer_refresh();
            });
            y += 80;
        }
        c.text(tr(en, "sudo：始终禁用\n第三方应用提权需系统确认与 Root 密码。",
                  "sudo: always disabled\nApp elevation requires system confirmation and a Root "
                  "password."),
               {28, y, w - 56, 62}, 13, t.muted);
        c.text(developer_message_, {28, y + 66, w - 56, 70}, 13, t.muted);
        return;
    }
    if (developer_view_ == DeveloperView::root_apps ||
        developer_view_ == DeveloperView::root_history) {
        paint_root_apps(c);
        return;
    }
    action({20, 164, 96, 44}, tr(en, "上一级", "Back"), [this] { developer_back(); });
    action({w - 116, 164, 96, 44}, tr(en, "刷新", "Refresh"), [this] { developer_refresh(); });
    if (developer_view_ == DeveloperView::kernel && developer_data_.contains("value")) {
        c.text(developer_path_, {24, 214, w - 48, 38}, 14, t.muted);
        field(260, tr(en, "运行时参数值", "Runtime value"), developer_value_, 3, false);
        if (developer_data_.value("writable", false))
            action({20, 360, w - 40, 48}, tr(en, "验证密码并应用", "Authenticate and apply"),
                   [this] {
                       developer_auth({{"op", "sysctl"},
                                       {"area", "kernel"},
                                       {"path", developer_path_},
                                       {"value", developer_value_}});
                   });
        c.text(tr(en, "仅影响当前运行；重启恢复内核默认值。",
                  "Runtime only; reboot restores kernel defaults."),
               {24, 426, w - 48, 60}, 13, t.muted);
    } else if (developer_view_ == DeveloperView::services && !developer_selection_.empty()) {
        message_y = 708;
        c.text(developer_selection_, {24, 214, w - 48, 38}, 20, t.text, true);
        c.text(developer_data_.value("status", ""), {24, 258, w - 48, 62}, 12, t.muted);
        c.text(developer_data_.value("executable", ""), {24, 320, w - 48, 42}, 12, t.muted);
        for (int i = 0; i < 3; ++i) {
            std::string command = i == 0 ? "start" : i == 1 ? "stop" : "restart";
            action({20 + i * (w - 40) / 3, 378, (w - 52) / 3, 48},
                   i == 0   ? tr(en, "启动", "Start")
                   : i == 1 ? tr(en, "停止", "Stop")
                            : tr(en, "重启", "Restart"),
                   [this, command] {
                       developer_auth({{"op", "service_control"},
                                       {"area", "services"},
                                       {"name", developer_selection_},
                                       {"action", command}});
                   });
        }
        bool autostart = developer_data_.value("autostart", true);
        action({20, 446, w - 40, 48},
               tr(en, "开机启动：", "Autostart: ") +
                   (autostart ? tr(en, "开", "On") : tr(en, "关", "Off")),
               [this, autostart] { developer_data_["autostart"] = !autostart; });
        auto restart = developer_data_.value("restart", "never");
        action({20, 510, w - 40, 48}, tr(en, "重启策略：", "Restart: ") + restart, [this, restart] {
            developer_data_["restart"] = restart == "never"        ? "on-failure"
                                         : restart == "on-failure" ? "always"
                                                                   : "never";
        });
        int limit = developer_data_.value("restart_limit", 5);
        action({20, 574, w - 40, 48},
               tr(en, "重试上限：", "Retry limit: ") + std::to_string(limit) + "  +",
               [this, limit] { developer_data_["restart_limit"] = (limit + 1) % 11; });
        action({20, 638, w - 40, 48}, tr(en, "验证密码并保存配置", "Authenticate and save policy"),
               [this] {
                   developer_auth({{"op", "service_config"},
                                   {"area", "services"},
                                   {"name", developer_selection_},
                                   {"autostart", developer_data_.value("autostart", true)},
                                   {"restart", developer_data_.value("restart", "never")},
                                   {"restart_limit", developer_data_.value("restart_limit", 5)}});
               });
    } else {
        std::string heading =
            developer_view_ == DeveloperView::kernel    ? "/proc/sys/" + developer_path_
            : developer_view_ == DeveloperView::modules ? tr(en, "内核模块", "Kernel modules")
            : developer_view_ == DeveloperView::core    ? developer_data_.value("kernel", "")
                                                        : tr(en, "系统服务", "System services");
        c.text(heading, {24, 214, w - 48, 38}, 16, t.text, true);
        double y = 272;
        if (developer_view_ == DeveloperView::modules) {
            action({20, y, w - 40, 44},
                   modules_available_
                       ? tr(en, "可用模块 · 切换到已加载", "Available · show loaded")
                       : tr(en, "已加载 · 切换到可用模块", "Loaded · show available"),
                   [this] {
                       modules_available_ = !modules_available_;
                       developer_offset_ = 0;
                       developer_refresh();
                   });
            y += 60;
        }
        for (auto item : developer_data_.value("items", Json::array())) {
            std::string name = item.value("name", "");
            if (developer_view_ == DeveloperView::core) {
                c.text(name + (item.value("present", false) ? " ✓" : " —"), {24, y, w - 48, 28}, 17,
                       t.text, true);
                c.text(item.value("path", ""), {24, y + 28, w - 48, 28}, 12, t.muted);
            } else
                action({20, y, w - 40, 52}, name, [this, item, name] {
                    developer_message_.clear();
                    scroll = 0;
                    if (developer_view_ == DeveloperView::kernel) {
                        developer_path_ = item.at("path");
                        developer_offset_ = 0;
                        developer_refresh();
                    } else if (developer_view_ == DeveloperView::services) {
                        developer_selection_ = name;
                        developer_refresh();
                    } else
                        developer_auth({{"op", "module"},
                                        {"area", "modules"},
                                        {"name", name},
                                        {"action", modules_available_ ? "load" : "unload"}});
                });
            y += 64;
        }
        if (developer_view_ != DeveloperView::core) {
            if (developer_offset_)
                action({20, y, (w - 48) / 2, 44}, tr(en, "上一页", "Previous"), [this] {
                    developer_offset_ = std::max(0, developer_offset_ - 12);
                    scroll = 0;
                    developer_refresh();
                });
            if (developer_data_.value("more", false))
                action({28 + (w - 48) / 2, y, (w - 48) / 2, 44}, tr(en, "下一页", "Next"), [this] {
                    developer_offset_ += 12;
                    scroll = 0;
                    developer_refresh();
                });
        }
        message_y = y + 60;
    }
    c.text(developer_message_, {24, message_y, w - 48, 80}, 13, t.muted);
}
} // namespace leon::apps
