#include "application.hpp"
#include <cctype>

using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
void SettingsApplication::applications_start(const std::string &operation, const std::string &id) {
    if (applications_job_.valid())
        return;
    applications_operation_ = operation;
    applications_subject_ = id;
    applications_failed_ = false;
    applications_ticks_ = 0;
    const char *override = std::getenv("LEON_APPS_SOCKET");
    std::string endpoint = override ? override : "/run/leon/apps.sock";
    applications_job_ = std::async(std::launch::async, [endpoint, operation, id] {
        try {
            Json request = {{"op", operation}};
            if (!id.empty())
                request["id"] = id;
            return service_rpc(endpoint, request);
        } catch (const std::exception &e) {
            return Json{{"ok", false}, {"error", e.what()}};
        }
    });
    app.redraw();
}
void SettingsApplication::applications_refresh() {
    applications_start(application_id_.empty() ? "app_list" : "app_info", application_id_);
}
void SettingsApplication::applications_poll() {
    if (!applications_confirmation_.empty() && !app.focused()) {
        applications_confirmation_.clear();
        app.redraw();
    }
    if (applications_job_.valid() &&
        applications_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = applications_job_.get();
        auto operation = applications_operation_, id = applications_subject_;
        if (!result.value("ok", false)) {
            applications_failed_ = true;
            applications_message_ = tr(en, "操作失败：", "Request failed: ") +
                                    result.value("error", "Unknown error");
            if (operation == "app_list") {
                applications_loaded_ = false;
                applications_ = Json::array();
            }
            if (operation == "app_info" && id == application_id_)
                application_ = Json::object();
        } else if (operation == "app_list") {
            applications_ = result.at("apps");
            applications_loaded_ = true;
        } else if (operation == "app_info") {
            if (id == application_id_)
                application_ = result.at("app");
        } else {
            applications_message_ = operation == "app_stop" ? tr(en, "应用已停止", "App stopped")
                                    : operation == "app_clear"
                                        ? tr(en, "数据和权限已重置", "Data and permissions reset")
                                        : tr(en, "应用已卸载", "App uninstalled");
            if (operation == "app_uninstall" && id == application_id_) {
                application_id_.clear();
                application_ = Json::object();
                scroll = 0;
            }
            if (page_ == Page::applications)
                applications_refresh();
        }
        // Returning to the list during an in-flight info request must not leave
        // the newly opened list waiting indefinitely for an unrelated result.
        if (page_ == Page::applications && application_id_.empty() && !applications_loaded_ &&
            operation == "app_info" && !applications_job_.valid() && !applications_failed_)
            applications_refresh();
        app.redraw();
    }
    if (page_ == Page::applications && !applications_failed_ && !applications_job_.valid() &&
        applications_confirmation_.empty() && !keyboard && ++applications_ticks_ >= 25)
        applications_refresh();
}
void SettingsApplication::applications_open(const std::string &id) {
    if (applications_job_.valid())
        return;
    transition_content();
    application_id_ = id;
    application_ = Json::object();
    applications_confirmation_.clear();
    applications_message_.clear();
    keyboard = false;
    scroll = 0;
    applications_refresh();
    app.redraw();
}
bool SettingsApplication::applications_back() {
    if (keyboard)
        keyboard = false;
    else if (!applications_confirmation_.empty())
        applications_confirmation_.clear();
    else if (!application_id_.empty()) {
        transition_content(-32);
        application_id_.clear();
        application_ = Json::object();
        applications_message_.clear();
        applications_refresh();
    } else
        return false;
    scroll = 0;
    app.redraw();
    return true;
}
void SettingsApplication::applications_text(const std::string &text) {
    if (!keyboard || !application_id_.empty())
        return;
    if (text == "\n" || text == "\r")
        keyboard = false;
    else if (text == "\b")
        backspace(applications_query_);
    else if (applications_query_.size() + text.size() <= 128)
        applications_query_ += text;
    scroll = 0;
    app.redraw();
}
void SettingsApplication::applications_confirm(const std::string &operation) {
    if (applications_job_.valid() || application_.empty() || application_id_.empty())
        return;
    std::string action = operation == "app_clear" ? "clear" : "uninstall";
    if (!application_.at("actions").value(action, false))
        return;
    transition_content();
    applications_confirmation_ = operation;
    applications_message_.clear();
    keyboard = false;
    scroll = 0;
    app.redraw();
}
std::vector<Json> SettingsApplication::visible_applications() const {
    auto fold = [](std::string text) {
        for (char &c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    auto query = fold(applications_query_);
    std::vector<Json> items;
    for (const auto &item : applications_) {
        auto text = item.at("id").get<std::string>() + " " + item.at("name").get<std::string>() +
                    " " + item.at("name_en").get<std::string>();
        if (query.empty() || fold(text).find(query) != std::string::npos)
            items.push_back(item);
    }
    return items;
}
double SettingsApplication::applications_bottom() const {
    if (!applications_confirmation_.empty())
        return 580;
    if (application_id_.empty())
        return std::max(420.0, 280.0 + visible_applications().size() * 100);
    return application_.empty() ? 460
                               : std::max(960.0, 918.0 + application_.at("permissions").size() * 64);
}
} // namespace leon::apps
