#include "policy.hpp"
#include "auth.h"
#include "daemon.h"
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>
using leon::Json;
namespace leon::notifications {
namespace {
struct State {
    Json settings = {
        {"sound", true}, {"dnd", false}, {"lockscreen", "hide"}, {"apps", Json::object()}};
    std::string path;
    bool pending{};
    long long next{};
    std::string audio_error;
};
State &state(const notification_store &s) { return *static_cast<State *>(s.policy); }
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool valid(const Json &j) {
    if (!j.is_object() || j.size() != 4 || !j.contains("sound") || !j["sound"].is_boolean() ||
        !j.contains("dnd") || !j["dnd"].is_boolean() || !j.contains("lockscreen") ||
        (j["lockscreen"] != "show" && j["lockscreen"] != "hide" && j["lockscreen"] != "none") ||
        !j.contains("apps") || !j["apps"].is_object() || j["apps"].size() > 512)
        return false;
    for (auto it = j["apps"].begin(); it != j["apps"].end(); ++it)
        if (!leon::valid_id(it.key()) || !it->is_object() || it->size() != 2 ||
            !it->contains("enabled") || !(*it)["enabled"].is_boolean() || !it->contains("sound") ||
            !(*it)["sound"].is_boolean())
            return false;
    return true;
}
void save(State &s, const Json &next) {
    auto directory = std::filesystem::path(s.path).parent_path();
    require(!lm_mkdir(directory.c_str(), 0700), "cannot prepare notification policy directory");
    std::string text = Json{{"format", 1}, {"settings", next}}.dump() + "\n";
    require(!lm_write_text(s.path.c_str(), text.c_str()), "cannot persist notification policy");
    s.settings = next;
    s.pending = false;
}
void purge(notification_store &s, const std::string &app) {
    notification_store copy{};
    copy.policy = s.policy;
    for (size_t i = 0; i < s.count; ++i) {
        auto item = notification_store_at(&s, i);
        if (app != item->app)
            notification_store_add(&copy, item->app, item->title, item->title_size);
    }
    s = copy;
}
} // namespace
void load(notification_store &s, const notification_options &o) {
    auto data = new State;
    s.policy = data;
    data->path = (std::filesystem::path(o.root) / "system/notifications.json").string();
    struct stat st;
    if (lstat(data->path.c_str(), &st)) {
        require(errno == ENOENT, "cannot inspect notification policy");
        return;
    }
    require(S_ISREG(st.st_mode) && st.st_size <= 131072, "invalid notification policy file");
    auto j = leon::read_json(data->path);
    require(j.is_object() && j.size() == 2 && j.value("format", 0) == 1 && j.contains("settings") &&
                valid(j["settings"]),
            "invalid notification policy");
    data->settings = j["settings"];
}
void unload(notification_store &s) {
    delete static_cast<State *>(s.policy);
    s.policy = nullptr;
}
bool enabled(const notification_store &s, const std::string &app) {
    auto &apps = state(s).settings["apps"];
    return !apps.contains(app) || apps[app]["enabled"].get<bool>();
}
void alert(notification_store &store, const std::string &app) {
    auto &s = state(store);
    auto &j = s.settings;
    if (!j["sound"].get<bool>() || j["dnd"].get<bool>() || !enabled(store, app) ||
        (j["apps"].contains(app) && !j["apps"][app]["sound"].get<bool>()))
        return;
    if (lm_now_ms() >= s.next)
        s.pending = true;
}
void tick(notification_store &store, const notification_options &o) {
    auto &s = state(store);
    if (!s.pending)
        return;
    s.pending = false;
    s.next = lm_now_ms() + 2000;
    if (!*o.audio_socket)
        return;
    auto request = lm_object();
    lm_set_string(request, "op", "notification_sound");
    auto reply = lm_rpc_bounded(o.audio_socket, request, 200);
    s.audio_error = lm_boolean(lm_get(reply, "ok"), 0)
                        ? ""
                        : lm_string(lm_get(reply, "error"), "audio service unavailable");
    lm_free(request);
    lm_free(reply);
}
std::string lockscreen(const notification_store &s) { return state(s).settings["lockscreen"]; }
bool locked(const notification_options &o, std::string &epoch) {
    if (!*o.auth_socket)
        return !o.development;
    char response[LEON_AUTH_LIMIT];
    auth_state auth{};
    if (auth_exchange(o.auth_socket, "{\"op\":\"status\"}\n", response, 80) ||
        auth_decode(response, &auth) || !auth.ok)
        return true;
    epoch = auth.epoch;
    return auth.locked;
}
Json policy(notification_store &store, const notification_options &o, uid_t uid, const Json &r) {
    require(uid == 0 || uid == 11000, "permission denied");
    auto &s = state(store);
    std::string op = r.at("op");
    auto next = s.settings;
    if (op == "configure") {
        auto changes = r.at("values");
        require(changes.is_object() && !changes.empty(), "nonempty notification settings required");
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            require(it.key() == "sound" || it.key() == "dnd" || it.key() == "lockscreen",
                    "unknown notification setting");
            next[it.key()] = it.value();
        }
        require(valid(next), "invalid notification settings");
        save(s, next);
    } else if (op == "app_policy") {
        auto app = r.at("app").get<std::string>();
        leon::Packages(o.root, o.builtin).lookup(app);
        auto values = next["apps"].value(app, Json{{"enabled", true}, {"sound", true}});
        bool changed = false;
        for (auto it = r.begin(); it != r.end(); ++it) {
            if (it.key() == "op" || it.key() == "app")
                continue;
            require(it.key() == "enabled" || it.key() == "sound",
                    "unknown app notification setting");
            require(it->is_boolean(), "notification switches must be boolean");
            values[it.key()] = it.value();
            changed = true;
        }
        require(changed, "missing app notification setting");
        next["apps"][app] = values;
        require(valid(next), "notification policy capacity reached");
        save(s, next);
        if (!values["enabled"].get<bool>())
            purge(store, app);
    } else if (op == "test_sound") {
        if (!s.settings["sound"].get<bool>() || s.settings["dnd"].get<bool>())
            return {{"ok", false},
                    {"error", "notification sounds are disabled or do not disturb is enabled"}};
        s.pending = true;
    } else
        require(op == "notification_policy", "unknown operation");
    Json apps = Json::array();
    for (const auto &app : leon::Packages(o.root, o.builtin).list()) {
        auto id = app.at("id").get<std::string>();
        auto value = s.settings["apps"].value(id, Json{{"enabled", true}, {"sound", true}});
        apps.push_back({{"id", id},
                        {"name", app.at("name")},
                        {"name_en", app.value("name_en", app.at("name"))},
                        {"enabled", value["enabled"]},
                        {"sound", value["sound"]}});
    }
    return {{"ok", true}, {"settings", s.settings}, {"apps", apps}, {"audio_error", s.audio_error}};
}
} // namespace leon::notifications
extern "C" int notification_policy_load(notification_store *s, const notification_options *o) {
    try {
        leon::notifications::load(*s, *o);
        return 0;
    } catch (...) {
        leon::notifications::unload(*s);
        return -1;
    }
}
extern "C" void notification_policy_free(notification_store *s) { leon::notifications::unload(*s); }
extern "C" void notification_policy_tick(notification_store *s, const notification_options *o) {
    leon::notifications::tick(*s, *o);
}
