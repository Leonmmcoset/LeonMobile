#include "application.hpp"
#include "log.h"
#include "wire.h"
#include "request.hpp"
#include <cstring>
#include <memory>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
namespace {
void wipe(Json &j) {
    if (j.is_string()) {
        auto &s = j.get_ref<std::string &>();
        erase_secret(s);
    } else if (j.is_structured())
        for (auto &item : j)
            wipe(item);
    j = nullptr;
}
Json request(const Json &j) {
    const char *override = std::getenv("LEON_LOG_SOCKET");
    auto response = std::make_unique<char[]>(LEON_LOG_RESPONSE);
    auto text = j.dump() + "\n";
    if (leon_log_exchange(override ? override : LEON_LOG_SOCKET, text.c_str(), response.get(),
                          LEON_LOG_RESPONSE, 2800))
        return {{"ok", false}, {"error", "unavailable"}};
    Json result;
    try {
        result = Json::parse(response.get());
    } catch (...) {
        result = {{"ok", false}, {"error", "invalid response"}};
    }
    root_clear(response.get(), LEON_LOG_RESPONSE);
    return result;
}
}
void SettingsApplication::diagnostics_clear() {
    ++diagnostics_generation_;
    wipe(diagnostics_data_);
    diagnostics_data_ = Json::object();
    wipe(diagnostics_detail_);
    diagnostics_ticks_ = 0;
}
void SettingsApplication::diagnostics_refresh() {
    if (diagnostics_job_.valid()) {
        diagnostics_pending_ = true;
        return;
    }
    diagnostics_pending_ = false;
    struct rlimit cores {0, 0};
    setrlimit(RLIMIT_CORE, &cores);
    prctl(PR_SET_DUMPABLE, 0);
    diagnostics_job_generation_ = diagnostics_generation_;
    diagnostics_failed_ = false;
    diagnostics_ticks_ = 0;
    auto tab = diagnostics_tab_;
    bool previous = diagnostics_previous_, warnings = diagnostics_warnings_;
    diagnostics_job_ = std::async(std::launch::async, [tab, previous, warnings] {
        Json data = request({{"op", "status"}});
        if (!data.value("ok", false) || data.value("access", "own") != "system")
            return Json{{"ok", false}, {"error", "locked or unavailable"}};
        Json query{{"op", tab == DiagnosticsTab::logs ? "query"
                          : tab == DiagnosticsTab::services ? "services" : "crashes"}};
        if (tab == DiagnosticsTab::logs) {
            query["boot"] = previous ? "previous" : "current";
            query["limit"] = 64;
            query["tail"] = true;
            query["max_level"] = warnings ? 4 : 7;
        }
        auto result = request(query);
        if (!result.value("ok", false)) {
            wipe(data);
            return result;
        }
        // logd repeats the authorization check for the second query.
        if (tab == DiagnosticsTab::logs && result.value("access", "own") != "system") {
            wipe(data);
            wipe(result);
            return Json{{"ok", false}, {"error", "locked"}};
        }
        data["result"] = std::move(result);
        return data;
    });
    app.redraw();
}
void SettingsApplication::diagnostics_poll() {
    bool active = page_ == Page::diagnostics && app.focused();
    if (!active) {
        if (!diagnostics_data_.empty() || !diagnostics_detail_.is_null()) {
            diagnostics_clear();
            app.redraw();
        }
        diagnostics_auth_ok_ = false;
    }
    if (diagnostics_auth_job_.valid() &&
        diagnostics_auth_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto state = diagnostics_auth_job_.get();
        diagnostics_auth_ok_ = active && state.ok && !state.locked;
        if (!diagnostics_auth_ok_ || (!diagnostics_epoch_.empty() &&
            (diagnostics_epoch_ != state.epoch || diagnostics_auth_generation_ != state.generation))) {
            diagnostics_clear();
            if (active && !diagnostics_auth_ok_) {
                diagnostics_failed_ = true;
                diagnostics_data_ = {{"ok", false}};
            }
            app.redraw();
        }
        diagnostics_epoch_ = state.epoch;
        diagnostics_auth_generation_ = state.generation;
    }
    if (active && !diagnostics_auth_job_.valid())
        diagnostics_auth_job_ = std::async(std::launch::async, [] {
            return device_request({{"op", "status"}}, 150);
        });
    if (diagnostics_job_.valid() &&
        diagnostics_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = diagnostics_job_.get();
        if (active && diagnostics_auth_ok_ && diagnostics_job_generation_ == diagnostics_generation_) {
            wipe(diagnostics_data_);
            diagnostics_failed_ = !result.value("ok", false);
            diagnostics_data_ = std::move(result);
            app.redraw();
        }
        wipe(result);
        if (active && diagnostics_auth_ok_ && (diagnostics_pending_ || diagnostics_data_.empty()))
            diagnostics_refresh();
    }
    if (active && diagnostics_auth_ok_ && !diagnostics_job_.valid() && diagnostics_detail_.is_null()) {
        ++diagnostics_ticks_;
        if ((!diagnostics_failed_ && diagnostics_data_.empty()) || diagnostics_pending_ ||
            (diagnostics_ticks_ >= 5 && diagnostics_live_ &&
             diagnostics_tab_ == DiagnosticsTab::logs && !diagnostics_previous_))
            diagnostics_refresh();
    }
}
bool SettingsApplication::diagnostics_back() {
    if (diagnostics_detail_.is_null())
        return false;
    wipe(diagnostics_detail_);
    scroll = 0;
    app.redraw();
    return true;
}
} // namespace leon::apps
