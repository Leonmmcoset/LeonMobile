#include "management.hpp"
#include "internal.hpp"
#include "root.hpp"
#include "sandbox.h"
#include "storage.hpp"
#include <chrono>
#include <fstream>
#include <leon/ipc.hpp>
#include <thread>

namespace leon::appd {
namespace {
Json describe(const Json &manifest, const Processes &processes) {
    std::string id = manifest.at("id");
    bool builtin = manifest.at("builtin"), self = id == "org.leon.settings";
    bool running = processes.running().contains(id);
    return {{"id", id},
            {"name", manifest.at("name")},
            {"name_en", manifest.value("name_en", manifest.at("name").get<std::string>())},
            {"version", manifest.at("version")},
            {"builtin", builtin},
            {"source", builtin ? "system-image" : "local-unsigned"},
            {"running", running},
            {"actions", {{"stop", running && !self}, {"clear", !self}, {"uninstall", !builtin}}}};
}
Json portal(const Options &options, const Json &request) {
    auto reply = Json::parse(leon::request(options.portal_socket, request));
    if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "permission service rejected request"));
    return reply;
}
void stop_writers(Processes &processes, const Options &options, const std::string &id, uid_t uid) {
    processes.stop(id);
    if (options.development)
        return;
    fail(!leon_cgroup_kill(uid), "cannot stop application resource group");
    auto path = "/sys/fs/cgroup/leon/apps/" + std::to_string(uid) + "/cgroup.events";
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    do {
        std::ifstream events(path);
        if (!events.is_open()) {
            fail(!fs::exists(fs::path(path).parent_path()),
                 "cannot inspect application resource group");
            return; // Never launched.
        }
        std::string key;
        int value;
        while (events >> key >> value)
            if (key == "populated" && !value)
                return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("application processes are still exiting; retry");
}
} // namespace
Json managed_list(Packages &packages, const Processes &processes) {
    Json apps = Json::array();
    for (const auto &manifest : packages.list())
        apps.push_back(describe(manifest, processes));
    return {{"apps", apps}};
}
Json managed_info(Packages &packages, const Processes &processes, const Options &options,
                  const std::string &id) {
    auto manifest = packages.lookup(id);
    auto app = describe(manifest, processes);
    auto permissions = Json::array();
    for (const auto &name : manifest.value("permissions", Json::array()))
        permissions.push_back({{"name", name}, {"state", "unavailable"}});
    bool permissions_available = false;
    try {
        permissions = portal(options, {{"op", "app_permissions"}, {"app", id}}).at("permissions");
        permissions_available = true;
    } catch (const std::exception &) {
        // Unavailable is distinct from denied; metadata/storage remain useful.
    }
    auto program = manifest.value("builtin", false) ? packages.executable(manifest)
                                                    : packages.root / "apps" / id;
    Json program_usage;
    try {
        // Only image-owned executable paths may be resolved; data walks never
        // follow links. Some images/native fixtures provide executable links.
        if (manifest.value("builtin", false))
            program = fs::canonical(program);
        program_usage = storage_usage(program, false);
    } catch (const std::exception &e) {
        program_usage = {{"available", false}, {"error", e.what()}};
    }
    app["permissions"] = permissions;
    app["permissions_available"] = permissions_available;
    app["storage"] = {{"package", program_usage},
                      {"data", storage_usage(packages.root / "data" / id)},
                      {"runtime", storage_usage(fs::path(options.runtime_prefix) /
                                                std::to_string(manifest.at("uid").get<uid_t>()))}};
    return {{"app", app}};
}
void manage_application(Packages &packages, Processes &processes, const Options &options,
                        const std::string &op, const std::string &id, bool purge) {
    auto manifest = packages.lookup(id); // Reject invalid/uninstalled/protected before stopping.
    fail(id != "org.leon.settings", "cannot stop or reset Settings itself");
    bool uninstall = op == "app_uninstall";
    fail(!uninstall || !manifest.value("builtin", false), "cannot remove a system application");
    uid_t uid = manifest.at("uid");
    stop_writers(processes, options, id, uid);
    if (manifest.contains("root"))
        revoke_root(options, id, op != "app_stop");
    if (op == "app_stop")
        return;
    // Root-only reset has no callback to apps.sock: avoid a broker/portal cycle.
    // A failed reset must never publish a successful data clear or uninstall.
    portal(options, {{"op", "reset_app"}, {"app", id}});
    erase_private_tree(options.runtime_prefix, std::to_string(uid));
    if (!uninstall || purge)
        erase_private_tree(packages.root / "data", id);
    if (uninstall)
        packages.remove(id, false);
}
} // namespace leon::appd
