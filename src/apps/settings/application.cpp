#include "application.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>

using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
SettingsApplication::SettingsApplication()
    : ApplicationWindow("org.leon.settings", "设置", "Settings"),
      device_(settings::read_device_info()) {}
void SettingsApplication::open(Page page) {
    transition_content();
    history_.push_back(page_);
    page_ = page;
    platform_data_ = Json::object();
    network_field_ = -1; network_loaded_ = false;
    platform_refresh();
    keyboard = false;
    scroll = 0;
    error.clear();
    if (page == Page::diagnostics) {
        diagnostics_clear();
        diagnostics_tab_ = DiagnosticsTab::logs;
        diagnostics_previous_ = diagnostics_warnings_ = diagnostics_live_ = false;
        diagnostics_refresh();
    }
    if (page == Page::applications) {
        application_id_.clear();
        application_ = Json::object();
        applications_confirmation_.clear();
        applications_query_.clear();
        applications_message_.clear();
        applications_refresh();
    }
    if (page == Page::security)
        security_refresh();
    if (page == Page::developer) {
        developer_view_ = DeveloperView::home;
        developer_path_.clear();
        developer_selection_.clear();
        developer_offset_ = 0;
        developer_refresh();
    }
    app.redraw();
}
void SettingsApplication::back() {
    if (page_ == Page::diagnostics && diagnostics_back())
        return;
    if (page_ == Page::diagnostics) {
        diagnostics_clear();
    }
    if (page_ == Page::applications && applications_back())
        return;
    if (page_ == Page::security && security_dialog_) {
        clear_device_fields();
        app.redraw();
        return;
    }
    if (page_ == Page::developer && (root_dialog_ || developer_view_ != DeveloperView::home)) {
        developer_back();
        return;
    }
    if (keyboard)
        keyboard = false;
    else if (!history_.empty()) {
        transition_content(-32);
        page_ = history_.back();
        history_.pop_back();
    } else
        query_.clear();
    scroll = 0;
    app.redraw();
}
void SettingsApplication::text_input(const std::string &text) {
    if (page_ == Page::network && network_field_ >= 0) {
        if (text == "\n") { keyboard = false; network_field_ = -1; }
        else if (text == "\b") backspace(network_fields_[network_field_]);
        else if (network_fields_[network_field_].size() + text.size() <= 160) network_fields_[network_field_] += text;
        app.redraw(); return;
    }
    if (page_ == Page::applications) {
        applications_text(text);
        return;
    }
    if (page_ == Page::security) {
        security_text(text);
        return;
    }
    if (page_ == Page::developer) {
        developer_text(text);
        return;
    }
    if (page_ != Page::home || !keyboard)
        return;
    if (text == "\n")
        keyboard = false;
    else if (text == "\b")
        backspace(query_);
    else if (query_.size() + text.size() <= 128)
        query_ += text;
    scroll = 0;
    app.redraw();
}
bool SettingsApplication::key_event(const KeyEvent &event) {
    if (event.symbol != XKB_KEY_Escape && event.symbol != XKB_KEY_XF86Back)
        return false;
    back();
    return true;
}
void SettingsApplication::on_tick() {
    platform_poll();
    diagnostics_poll();
    applications_poll();
    developer_poll();
    if (security_dialog_ && !app.focused()) {
        clear_device_fields();
        app.redraw();
    }
    if (page_ == Page::security)
        security_refresh();
    if (root_dialog_ && !app.focused()) {
        clear_root_fields();
        app.redraw();
    }
    if (++status_ticks_ < 10)
        return;
    status_ticks_ = 0;
    platform_refresh();
    auto info = settings::read_device_info();
    if (device_ != info) {
        device_ = std::move(info);
        app.redraw();
    }
}
void SettingsApplication::save_preference(const std::string &key, Json value) {
    // The window's input dispatcher reports errors from this single request.
    prefs = rpc({{"op", "set"}, {"key", key}, {"value", std::move(value)}}).at("settings");
    en = prefs.value("language", "zh-CN") == "en";
    app.dark = prefs.value("dark", false);
    app.reduce_motion(prefs.value("reduce_motion", false));
    app.redraw();
}
std::string SettingsApplication::title(Page page) const {
    const auto &item = settings::entry(page);
    return std::string(en ? item.english : item.chinese);
}
std::string SettingsApplication::summary(Page page) const {
    switch (page) {
    case Page::network:
        return device_.connected
                   ? tr(en, "已连接 · " + device_.interface, "Connected · " + device_.interface)
                   : tr(en, "未连接", "Not connected");
    case Page::display:
        return app.dark ? tr(en, "深色主题", "Dark theme") : tr(en, "浅色主题", "Light theme");
    case Page::sound:
        return tr(en, "播放音量 · ", "Playback volume · ") +
               std::to_string(prefs.value("volume", 70)) + "%";
    case Page::storage:
        return device_.data.available
                   ? tr(en, "可用 ", "Available: ") + settings::format_bytes(device_.data.free)
                   : tr(en, "存储信息不可用", "Storage information unavailable");
    case Page::system:
        return tr(en, "语言、系统导航", "Languages, system navigation");
    case Page::language:
        return en ? "English" : "简体中文";
    case Page::navigation:
        return prefs.value("navigation", "gestures") == "gestures"
                   ? tr(en, "手势导航", "Gesture navigation")
                   : tr(en, "三键导航", "3-button navigation");
    case Page::about:
        return "LeonMobile 1.0.0";
    case Page::security:
        return tr(en, "设备 PIN、密码与自动锁定", "Device PIN, password and locking");
    case Page::developer:
        return tr(en, "Root、内核、模块与服务", "Root, kernel, modules and services");
    case Page::applications:
        return tr(en, "应用信息、权限与存储", "App info, permissions and storage");
    case Page::diagnostics:
        return tr(en, "日志、服务状态与崩溃记录", "Logs, service health and crash reports");
    default:
        return {};
    }
}
std::vector<Page> SettingsApplication::visible_entries() const {
    std::vector<Page> pages;
    for (const auto &item : settings::entries())
        if (query_.empty() ? item.home : settings::matches(item, query_))
            pages.push_back(item.page);
    return pages;
}
void SettingsApplication::content_hit(Rect bounds, std::function<void(double, double)> action) {
    bounds.y -= scroll;
    double right = std::min(bounds.x + bounds.w, viewport_.x + viewport_.w);
    double bottom = std::min(bounds.y + bounds.h, viewport_.y + viewport_.h);
    bounds.x = std::max(bounds.x, viewport_.x);
    bounds.y = std::max(bounds.y, viewport_.y);
    bounds.w = right - bounds.x;
    bounds.h = bottom - bounds.y;
    if (bounds.w > 0 && bounds.h > 0)
        click(hits, bounds, std::move(action));
}
} // namespace leon::apps
