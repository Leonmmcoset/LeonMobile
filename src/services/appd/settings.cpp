#include "settings.hpp"
#include "internal.hpp"
#include <leon/ipc.hpp>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
namespace leon::appd {
Settings::Settings(const Options &options)
    : path_(options.root / "settings.json"),
      socket_(options.settings_socket.empty() ? (options.development ? "" : "/run/leon/settings.sock") : options.settings_socket) {
    if (!socket_.empty()) return;
    Json defaults{{"dark", false},
                  {"language", "zh-CN"},
                  {"navigation", "gestures"},
                  {"volume", 70},
                  {"reduce_motion", false}, {"clock_24h", true}};
    if (!fs::exists(path_))
        atomic_json(path_, defaults);
}
Json Settings::read() const {
    if (!socket_.empty()) {
        auto result = Json::parse(leon::request(socket_, {{"op", "status"}}));
        fail(result.value("ok", false), result.value("error", "settings service unavailable").c_str());
        return result.at("settings");
    }
    auto state = read_json(path_);
    if (!state.contains("reduce_motion"))
        state["reduce_motion"] = false;
    if (!state.contains("clock_24h")) state["clock_24h"] = true;
    return state;
}
Json Settings::set(const Json &request) {
    if (!socket_.empty()) {
        auto result = Json::parse(leon::request(socket_, request));
        fail(result.value("ok", false), result.value("error", "settings service unavailable").c_str());
        return result.at("settings");
    }
    auto value = request.at("value");
    std::string key = request.at("key");
    bool valid = ((key == "dark" || key == "reduce_motion" || key == "clock_24h") && value.is_boolean()) ||
                 (key == "language" && (value == "zh-CN" || value == "en")) ||
                 (key == "navigation" && (value == "gestures" || value == "buttons")) ||
                 (key == "volume" && value.is_number_integer() && value.get<int>() >= 0 &&
                  value.get<int>() <= 100);
    fail(valid, "invalid setting");
    auto state = read();
    state[key] = value;
    leon::atomic_json(path_, state);
    return state;
}
} // namespace leon::appd
