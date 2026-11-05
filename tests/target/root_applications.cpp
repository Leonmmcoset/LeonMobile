// Injected into disposable QEMU disks only. No test daemon or privilege helper is shipped.
#include "wire.h"
#include "log.h"
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using Json = nlohmann::json;
namespace fs = std::filesystem;
static const char *password = "RootAppsTest!2026";
static const char *id = "org.example.rootprobe";
static const char *root_socket = LEON_ROOT_SOCKET;
static std::ofstream report;
static std::string read(const fs::path &path) {
    std::ifstream f(path);
    std::ostringstream out;
    out << f.rdbuf();
    return out.str();
}
static void check(bool condition, const std::string &reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
static void wait_for(const std::function<bool()> &predicate, const std::string &reason,
                     unsigned seconds = 10) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    do {
        if (predicate())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error(reason);
}
static Json rpc(const char *socket, Json request) {
    int fd = root_connect(socket);
    check(fd >= 0, "cannot connect to service");
    std::string text = request.dump() + "\n";
    char response[LEON_ROOT_MESSAGE];
    int failed =
        root_send(fd, text.c_str(), -1) || root_receive(fd, response, sizeof(response), nullptr);
    root_clear(text.data(), text.size());
    close(fd);
    check(!failed, "service transport failed");
    return Json::parse(response);
}
static Json root(Json request) { return rpc(root_socket, std::move(request)); }
static Json apps(Json request) { return rpc("/run/leon/apps.sock", std::move(request)); }
static void passed(const char *label) { report << "PASS " << label << std::endl; }
static void run(const std::vector<std::string> &args) {
    pid_t child = fork();
    check(child >= 0, "cannot fork fixture tool");
    if (!child) {
        std::vector<char *> argv;
        for (const auto &a : args)
            argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        int null = open("/dev/null", O_RDWR);
        dup2(null, 0);
        dup2(null, 1);
        dup2(null, 2);
        execv(argv[0], argv.data());
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    check(WIFEXITED(status) && !WEXITSTATUS(status), "fixture tool failed: " + args[0]);
}
static bool alive(pid_t pid) { return pid > 1 && fs::exists("/proc/" + std::to_string(pid)); }
static Json catalog() {
    auto response = apps({{"op", "list"}});
    for (auto item : response.at("apps"))
        if (item.at("id") == id)
            return item;
    throw std::runtime_error("probe package absent");
}
static fs::path home() { return fs::path("/data/leon/data") / id; }
static long long log_after;
static std::vector<std::string> log_messages() {
    std::vector<std::string> messages;
    auto response = std::make_unique<char[]>(LEON_LOG_RESPONSE);
    std::string request = Json{{"op", "query"}, {"source", id}, {"after", log_after},
                               {"limit", 128}}.dump() + "\n";
    if (!leon_log_exchange(LEON_LOG_SOCKET, request.c_str(), response.get(), LEON_LOG_RESPONSE, 1500))
        for (const auto &record : Json::parse(response.get()).value("records", Json::array()))
            messages.push_back(record.value("message", ""));
    return messages;
}
static std::string output() {
    std::string bytes;
    for (const auto &line : log_messages())
        try {
            auto j = Json::parse(line);
            auto hex = j.value("output_hex", "");
            for (size_t i = 0; i + 1 < hex.size(); i += 2)
                bytes.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        } catch (...) {
        }
    return bytes;
}
static pid_t helper_pid() {
    for (const auto &line : log_messages())
        try {
            auto j = Json::parse(line);
            if (j.value("state", "") == "running")
                return j.at("pid");
        } catch (...) {
        }
    return -1;
}
static pid_t launch(const std::string &mode = "wait") {
    {
        auto response = std::make_unique<char[]>(LEON_LOG_RESPONSE);
        check(!leon_log_exchange(LEON_LOG_SOCKET, "{\"op\":\"status\"}\n", response.get(),
                                 LEON_LOG_RESPONSE, 1500), "logd unavailable");
        log_after = Json::parse(response.get()).value("cursor", 0LL);
    }
    if (!fs::exists(home()))
        fs::create_directories(home());
    auto uid = catalog().at("uid").get<uid_t>();
    chown(home().c_str(), uid, uid);
    chmod(home().c_str(), 0700);
    auto file = home() / "root-request.json";
    {
        std::ofstream f(file);
        f << Json{{"op", "app_request"}, {"args", {mode}}}.dump();
    }
    chown(file.c_str(), uid, uid);
    chmod(file.c_str(), 0600);
    auto response = apps({{"op", "launch"}, {"id", id}});
    check(response.value("ok", false), "sandboxed probe launch failed");
    return response.at("pid").get<pid_t>();
}
static Json prompt() {
    Json found;
    wait_for(
        [&] {
            auto reply = root({{"op", "app_pending"}});
            for (const auto &r : reply.value("requests", Json::array()))
                if (r.at("id") == id) {
                    found = r;
                    return true;
                }
            return false;
        },
        "installed sandboxed client did not publish a Root request");
    return found;
}
static void approve(const Json &r) {
    auto reply = root(
        {{"op", "app_decide"}, {"token", r.at("token")}, {"allow", true}, {"password", password}});
    check(reply.value("ok", false), "trusted fixture confirmation rejected: " + reply.dump());
}
static void stopped() {
    wait_for(
        [] {
            return !apps({{"op", "list"}}).at("running").contains(id);
        },
        "application did not exit");
}
static void unlocked() {
    const char *endpoint = "/run/leon-auth/control.sock";
    auto grant = rpc(endpoint, {{"op", "authenticate"}, {"purpose", "unlock"}, {"secret", ""}});
    check(grant.value("ok", false), "cannot authenticate fixture session");
    check(rpc(endpoint, {{"op", "consume"}, {"purpose", "unlock"}, {"token", grant.at("token")}})
              .value("ok", false),
          "cannot unlock fixture session");
}
static void test() {
    check(!root({{"op", "status"}}).at("enabled").get<bool>(), "Root must start disabled");
    check(root({{"op", "enable"}, {"password", password}}).value("ok", false),
          "cannot enable fixture Root policy");
    unlocked();
    run({"/usr/bin/lmpkg", "install", "/usr/share/root-probe.lmpkg"});
    auto app = catalog();
    auto uid = app.at("uid").get<uid_t>();
    auto pid = launch("descendant");
    auto request = prompt();
    check(request.at("uid") == uid && request.at("pid") == pid, "request peer binding mismatch");
    auto status = read("/proc/" + std::to_string(pid) + "/status");
    check(status.find("NoNewPrivs:\t1") != std::string::npos &&
              status.find("Seccomp:\t2") != std::string::npos,
          "requester lost inherited sandbox");
    check(!root({{"op", "app_decide"},
                 {"token", request.at("token")},
                 {"allow", true},
                 {"password", "wrong"}})
               .value("ok", true),
          "wrong Root password accepted");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    approve(request);
    wait_for([] { return output().find("descendant_pid=") != std::string::npos; },
             "helper/descendant did not start");
    auto child = helper_pid();
    auto data = output();
    auto at = data.find("descendant_pid=");
    auto descendant = static_cast<pid_t>(std::stoi(data.substr(at + 15)));
    check(data.find("helper_uid=0") != std::string::npos &&
              data.find("extra_fds=0 stdin_eof=1 injected_env=0") != std::string::npos,
          "helper inherited credentials, descriptors, input or environment");
    check(read("/proc/" + std::to_string(child) + "/cgroup").find("/leon/root/s") !=
                  std::string::npos &&
              read("/proc/" + std::to_string(descendant) + "/cgroup").find("/leon/root/s") !=
                  std::string::npos,
          "Root descendants escaped their resource group");
    check(read("/proc/" + std::to_string(pid) + "/cgroup")
                  .find("/leon/apps/" + std::to_string(uid)) != std::string::npos,
          "requester moved out of application sandbox");
    check(root({{"op", "app_revoke"}, {"id", id}}).value("ok", false), "cannot revoke helper");
    wait_for([&] { return !alive(child) && !alive(descendant); },
             "revocation left detached Root descendants");
    stopped();
    check(!root({{"op", "app_decide"},
                 {"token", request.at("token")},
                 {"allow", true},
                 {"password", password}})
               .value("ok", true),
          "consumed token replayed");
    passed(
        "installed sandbox peer, exact request, fresh password, separate UID 0 helper, inherited "
        "FD/environment exclusion, cgroups, detached descendant revocation and replay denial");

    launch();
    request = prompt();
    check(root({{"op", "app_decide"},
                {"token", request.at("token")},
                {"allow", false},
                {"block", true}})
              .value("ok", false),
          "deny/block failed");
    stopped();
    run({"/usr/bin/leonctl", "restart", "root"});
    wait_for(
        [] {
            try {
                return root({{"op", "status"}}).value("ok", false);
            } catch (...) {
                return false;
            }
        },
        "rootd did not restart");
    check(root({{"op", "app_list"}}).at("apps").at(0).at("blocked").get<bool>(),
          "Root restriction lost across daemon restart");
    launch();
    stopped();
    check(helper_pid() < 0, "blocked request executed");
    check(root({{"op", "app_policy"}, {"id", id}, {"blocked", false}, {"password", password}})
              .value("ok", false),
          "cannot authenticate request policy change");
    passed("deny/block persistence, blocked caller failure and authenticated unblocking");

    launch();
    request = prompt();
    approve(request);
    wait_for([] { return helper_pid() > 1; }, "helper not started before lock");
    child = helper_pid();
    check(rpc("/run/leon-auth/control.sock", {{"op", "lock"}}).value("ok", false),
          "cannot lock fixture session");
    wait_for([&] { return !alive(child); }, "Root helper survived device lock");
    stopped();
    unlocked();
    launch();
    request = prompt();
    approve(request);
    wait_for([] { return helper_pid() > 1; }, "helper not started before crash");
    child = helper_pid();
    auto control = root({{"op", "inspect"}, {"area", "core"}});
    (void)control;
    // Kill the actual daemon; leond restarts it, cleaning worker cgroups and stale approvals.
    pid_t daemon = -1;
    for (const auto &entry : fs::directory_iterator("/proc")) {
        auto name = entry.path().filename().string();
        if (name.find_first_not_of("0123456789") == std::string::npos &&
            read(entry.path() / "comm") == "rootd\n") {
            auto cmd = read(entry.path() / "cmdline");
            if (cmd.find("/usr/sbin/rootd") != std::string::npos) {
                auto status = read(entry.path() / "status");
                auto at = status.find("PPid:");
                if (at != std::string::npos) {
                    auto parent = std::stoi(status.substr(at + 5));
                    if (read("/proc/" + std::to_string(parent) + "/comm") == "leond\n")
                        daemon = std::stoi(name);
                }
            }
        }
    }
    check(daemon > 1, "cannot identify rootd parent");
    kill(daemon, SIGKILL);
    wait_for([&] { return !alive(child); }, "Root helper survived daemon crash");
    stopped();
    wait_for(
        [] {
            try {
                return root({{"op", "status"}}).value("ok", false);
            } catch (...) {
                return false;
            }
        },
        "rootd crash recovery failed");
    check(root({{"op", "app_pending"}}).value("requests", Json::array()).empty(),
          "approval survived daemon crash");
    passed(
        "device-lock cancellation, daemon crash, parent-death signals and stale-cgroup recovery");

    for (const auto &operation : {"app_stop", "app_clear", "app_uninstall"}) {
        launch("descendant");
        request = prompt();
        approve(request);
        wait_for([] { return output().find("descendant_pid=") != std::string::npos; },
                 "Root writers did not start before management action");
        child = helper_pid();
        data = output();
        at = data.find("descendant_pid=");
        descendant = std::stoi(data.substr(at + 15));
        check(apps({{"op", operation}, {"id", id}}).value("ok", false),
              std::string("management cleanup failed: ") + operation);
        check(!alive(child) && !alive(descendant),
              "successful management action left Root writers");
    }
    passed("real broker stop, data clear and uninstall wait for all Root cgroup writers");
    run({"/usr/bin/lmpkg", "install", "/usr/share/root-probe.lmpkg"});
    launch();
    request = prompt();
    auto old = request.at("installation");
    check(apps({{"op", "app_uninstall"}, {"id", id}}).value("ok", false),
          "cannot uninstall pending instance");
    run({"/usr/bin/lmpkg", "install", "/usr/share/root-probe.lmpkg"});
    launch();
    auto replacement = prompt();
    check(replacement.at("installation") != old,
          "same-version reinstallation reused Root identity");
    check(!root({{"op", "app_decide"},
                 {"token", request.at("token")},
                 {"allow", true},
                 {"password", password}})
               .value("ok", true),
          "old installation token authorized replacement");
    check(root({{"op", "app_abort"}, {"token", replacement.at("token")}}).value("ok", false),
          "trusted UI abort failed");
    stopped();
    check(apps({{"op", "app_uninstall"}, {"id", id}}).value("ok", false), "cannot remove fixture");
    passed("same-UID/same-version reinstall identity rotation and trusted system cancellation");
    check(root({{"op", "disable"}, {"password", password}}).value("ok", false),
          "cannot restore disabled Root");
    // Leave the cloned image's UI exercise at the production first-enable defaults.
    fs::remove("/data/leon/rootd/policy.json");
    check(rpc("/run/leon-auth/control.sock", {{"op", "lock"}}).value("ok", false),
          "cannot restore boot lock");
    auto history = root({{"op", "app_history"}}).dump();
    check(history.find(password) == std::string::npos &&
              history.find("output_hex") == std::string::npos,
          "audit leaked sensitive request data");
    passed("Root disabled/default credential restored, device locked and audit secret exclusion");
}
int main() {
    report.open("/run/leon/root-app-tests.txt");
    chmod("/run/leon/root-app-tests.txt", 0644);
    try {
        if (!fs::exists("/data/leon/root-app-target-complete")) {
            test();
            std::ofstream marker("/data/leon/root-app-target-complete");
            marker << "complete\n";
        }
        report << "ROOT_APP_TESTS_PASSED" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        report << "ROOT_APP_TESTS_FAILED " << e.what() << std::endl;
        return 1;
    }
}
