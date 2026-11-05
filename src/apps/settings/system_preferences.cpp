#include "application.hpp"
#include "widgets.hpp"
#include <cctype>
#include <ctime>
#include <leon/system.hpp>
using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
void SettingsApplication::preferences_enter() {
    ++preferences_generation_;
    preferences_data_ = Json::object();
    preferences_loaded_ = false;
    timezone_select_ = false;
    time_field_ = -1;
    timezone_query_.clear();
    preferences_message_.clear();
    preferences_refresh();
}
void SettingsApplication::preferences_refresh(Json command) {
    if (!command.empty()) {
        if (preferences_mutating_ || (page_ != Page::datetime && page_ != Page::notifications))
            return;
        preferences_mutating_ = true;
        preferences_pending_ = std::move(command);
        preferences_pending_page_ = page_;
        preferences_pending_generation_ = preferences_generation_;
        preferences_message_.clear();
        app.redraw();
    }
    if (preferences_job_.valid())
        return;
    auto page = page_;
    auto generation = preferences_generation_;
    if (!preferences_pending_.empty()) {
        command = std::move(preferences_pending_);
        preferences_pending_ = Json::object();
        page = preferences_pending_page_;
        generation = preferences_pending_generation_;
    } else if (page != Page::datetime && page != Page::notifications)
        return;
    preferences_page_ = page;
    preferences_job_generation_ = generation;
    preferences_job_mutating_ = !command.empty();
    preferences_job_ = std::async(std::launch::async, [page, command = std::move(command)] {
        const char *service = page == Page::datetime ? "time" : "notifications";
        Json result = Json::object();
        std::string message;
        if (!command.empty())
            try {
                system_request(service, command);
            } catch (const std::exception &e) {
                message = e.what();
            }
        try {
            result = system_request(
                service, {{"op", page == Page::datetime ? "status" : "notification_policy"}});
            if (page == Page::datetime)
                result["zones"] = system_request("time", {{"op", "timezones"}}).at("zones");
        } catch (const std::exception &e) {
            result["unavailable"] = true;
            message = e.what();
        }
        result["operation_error"] = message;
        result["operation"] = !command.empty();
        return result;
    });
    app.redraw();
}
void SettingsApplication::preferences_poll() {
    if (!preferences_job_.valid() ||
        preferences_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    auto result = preferences_job_.get();
    if (preferences_job_mutating_)
        preferences_mutating_ = false;
    if (page_ != preferences_page_ || preferences_job_generation_ != preferences_generation_) {
        preferences_refresh();
        return;
    }
    auto message = result.value("operation_error", "");
    if (!message.empty() || result.value("operation", false))
        preferences_message_ = message;
    bool server_editing = time_field_ == 1 ||
        (preferences_loaded_ && time_server_ != preferences_data_.value("server", ""));
    preferences_data_ = std::move(result);
    if (page_ == Page::datetime && !preferences_data_.value("unavailable", false)) {
        auto zone = preferences_data_.value("timezone", "Etc/UTC");
        setenv("TZ", zone.c_str(), 1);
        tzset();
        if (!server_editing)
            time_server_ = preferences_data_.value("server", "");
        if (!preferences_loaded_) {
            time_t now = preferences_data_.value("unix_seconds", int64_t(time(nullptr)));
            struct tm tm;
            localtime_r(&now, &tm);
            char text[32];
            strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm);
            time_manual_ = text;
        }
    }
    preferences_loaded_ = !preferences_data_.value("unavailable", false);
    app.redraw();
    if (!preferences_pending_.empty())
        preferences_refresh();
}
bool SettingsApplication::preferences_back() {
    if (page_ != Page::datetime && page_ != Page::notifications)
        return false;
    if (keyboard) {
        keyboard = false;
        time_field_ = -1;
        app.redraw();
        return true;
    }
    if (timezone_select_) {
        timezone_select_ = false;
        time_field_ = -1;
        scroll = 0;
        app.redraw();
        return true;
    }
    ++preferences_generation_;
    return false;
}
void SettingsApplication::time_text(const std::string &text) {
    if (time_field_ < 0 || !keyboard)
        return;
    auto &value = time_field_ == 0   ? time_manual_
                  : time_field_ == 1 ? time_server_
                                     : timezone_query_;
    if (text == "\n") {
        keyboard = false;
        time_field_ = -1;
    } else if (text == "\b")
        backspace(value);
    else if (value.size() + text.size() <= 120)
        value += text;
    if (time_field_ == 2)
        scroll = 0;
    app.redraw();
}
std::vector<std::string> SettingsApplication::visible_timezones() const {
    auto fold = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return text;
    };
    auto query = fold(timezone_query_);
    std::vector<std::string> zones;
    for (const auto &zone : preferences_data_.value("zones", Json::array())) {
        auto id = zone.at("id").get<std::string>();
        if (fold(id).find(query) != std::string::npos)
            zones.push_back(id);
    }
    return zones;
}
double SettingsApplication::preferences_bottom() const {
    if (page_ == Page::notifications)
        return 680 + preferences_data_.value("apps", Json::array()).size() * 152;
    if (!timezone_select_)
        return 1240;
    return 252 + visible_timezones().size() * 64;
}
void SettingsApplication::preference_toggle(Canvas &c, double y, const std::string &label,
                                            const std::string &detail, bool value,
                                            std::function<void()> action) {
    double w = c.width;
    auto t = c.theme;
    c.rounded({20, y, w - 40, 84}, t.surface, 24);
    c.text(label, {36, y + 10, w - 146, 30}, 18, t.text);
    c.text(detail, {36, y + 46, w - 146, 24}, 12, t.muted);
    settings::toggle(c, {w - 92, y + 27, 52, 30}, value, "preferences/" + label);
    if (!preferences_mutating_ && preferences_loaded_)
        content_hit({20, y, w - 40, 84}, [action](double, double) { action(); });
}
} // namespace leon::apps
