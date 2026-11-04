#include "broker.hpp"
#include "internal.hpp"
#include "management.hpp"
#include "sandbox.h"
#include <csignal>
#include <leon/ipc.hpp>
#include <unistd.h>
namespace leon::appd {
Broker::Broker(const Options &options)
    : options_(options), packages_(options.root, options.builtin), processes_(options),
      settings_(options) {
    if (!options_.development)
        fail(!leon_cgroup_reset(), "cannot clean stale application resource groups");
}
Json Broker::dispatch(const Json &request, uid_t caller) {
    processes_.reap();
    Json response;
    auto op = request.at("op").get<std::string>();
    bool shell = caller == 0 || caller == 1000;
    bool settings = shell || caller == 11000;
    if (op == "app_list" || op == "app_info" || op == "app_stop" || op == "app_clear" ||
        op == "app_uninstall") {
        fail(settings, "permission denied");
        if (op == "app_list")
            response = managed_list(packages_, processes_);
        else if (op == "app_info")
            response = managed_info(packages_, processes_, options_, request.at("id"));
        else {
            manage_application(packages_, processes_, options_, op, request.at("id"));
            response = {{"completed", op}, {"id", request.at("id")}};
        }
    } else if (op == "list") {
        fail(shell, "permission denied");
        response = {{"apps", packages_.list()},
                    {"running", processes_.running()},
                    {"applications", processes_.lifecycle().at("applications")}};
    } else if (op == "launch") {
        fail(shell, "permission denied");
        response = {{"pid", processes_.launch(packages_, request.at("id"))}};
    } else if (op == "reclaim") {
        fail(caller == 0, "permission denied");
        response = {{"reclaimed", processes_.reclaim()}};
    } else if (op == "foreground" || op == "background" || op == "save" || op == "lifecycle") {
        fail(shell, "permission denied");
        if (op == "foreground")
            processes_.foreground(request.at("id"));
        else if (op == "background")
            processes_.background();
        else if (op == "save")
            processes_.save(request.at("id"));
        response = processes_.lifecycle();
    } else if (op == "install") {
        fail(shell, "permission denied");
        packages_.install(request.at("file").get<std::string>());
    } else if (op == "remove") {
        fail(shell, "permission denied");
        std::string id = request.at("id");
        manage_application(packages_, processes_, options_, "app_uninstall", id,
                           request.value("purge", false));
    } else if (op == "close" || op == "stop") {
        fail(shell, "permission denied");
        std::string id = request.at("id");
        if (op == "close")
            processes_.request_stop(id);
        else
            processes_.stop(id);
        response = processes_.lifecycle();
    } else if (op == "settings")
        response = {{"settings", settings_.read()}};
    else if (op == "set") {
        fail(settings, "permission denied");
        response = {{"settings", settings_.set(request)}};
    } else if (op == "poweroff" || op == "reboot") {
        fail(shell, "permission denied");
        fail(!options_.development, "power operations disabled for development roots");
        response = Json::parse(leon::request("/run/leon/power.sock", {{"op", op}}));
        fail(response.value("ok", false), "power service rejected request");
    } else if (op == "notifications" || op == "notify") {
        // Keep the v1 application endpoint as a relay; notificationd alone
        // owns the queue and validates the original peer's app identity.
        Json relay;
        if (op == "notifications") {
            fail(shell, "permission denied");
            relay = {{"op", "notifications"}};
        } else
            relay = {{"op", "notify_for"}, {"uid", caller}, {"title", request.at("title")}};
        response = Json::parse(leon::request(options_.notifications_socket, relay));
        if (!response.value("ok", false))
            throw std::runtime_error(response.value("error", "notification service error"));
    } else
        throw std::runtime_error("unknown operation");
    response["ok"] = true;
    return response;
}
} // namespace leon::appd
