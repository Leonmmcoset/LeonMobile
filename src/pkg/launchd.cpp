#include "package.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <map>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
using leon::Json;
namespace fs = std::filesystem;
static volatile sig_atomic_t quitting;
static void on_signal(int) { quitting = 1; }
static void fail(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
static std::map<std::string, pid_t> running;
static std::string runtime_prefix = "/run/user";
static std::string wayland_socket = "/run/leon/wayland/wayland-0";

static void reap() {
    pid_t p;
    while ((p = waitpid(-1, nullptr, WNOHANG)) > 0) {
        for (auto i = running.begin(); i != running.end();)
            if (i->second == p) {
                kill(-p, SIGKILL);
                i = running.erase(i);
            } else
                ++i;
    }
}
static int run_tool(const std::vector<std::string> &args) {
    pid_t pid = fork();
    if (!pid) {
        setsid();
        if (setgroups(0, nullptr) || setgid(29) || setuid(1000))
            _exit(126);
        clearenv();
        setenv("PATH", "/usr/bin:/bin", 1);
        std::string runtime = runtime_prefix + "/1000";
        setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
        setenv("HOME", "/data/home/leon", 1);
        std::vector<char *> av;
        for (const auto &a : args)
            av.push_back(const_cast<char *>(a.c_str()));
        av.push_back(nullptr);
        execv(av[0], av.data());
        _exit(127);
    }
    if (pid < 0)
        return -1;
    int status = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (result < 0 && errno != EINTR)
            return -1;
        usleep(10000);
    }
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return -1;
}
static pid_t launch(leon::Packages &packages, const std::string &id) {
    reap();
    if (running.contains(id))
        return running[id];
    auto m = packages.lookup(id);
    unsigned uid = m.at("uid");
    fs::path home = packages.root / "data" / id;
    fs::create_directories(home);
    fail(!chown(home.c_str(), uid, uid) && !chmod(home.c_str(), 0700),
         "cannot create application home");
    std::string runtime = runtime_prefix + "/" + std::to_string(uid);
    fs::create_directories(runtime);
    fail(!chown(runtime.c_str(), uid, uid) && !chmod(runtime.c_str(), 0700),
         "cannot create runtime directory");
    auto exe = fs::canonical(packages.executable(m));
    std::vector<std::string> args{exe.string()};
    for (const auto &a : m.value("args", Json::array()))
        args.push_back(a.get<std::string>());
    /* Pin both executable and working directory to the selected version. */
    auto work = m.value("builtin", false) ? packages.builtin.parent_path() : exe.parent_path();
    pid_t pid = fork();
    fail(pid >= 0, "cannot fork application");
    if (!pid) {
        setsid();
        umask(077);
        int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (input >= 0) {
            dup2(input, STDIN_FILENO);
            close(input);
        }
        int log = open((home / "application.log").c_str(),
                       O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
        if (log >= 0) {
            fchown(log, uid, uid);
            dup2(log, 1);
            dup2(log, 2);
            close(log);
        }
        if (setgroups(0, nullptr) || setgid(uid) || setuid(uid) || chdir(work.c_str()))
            _exit(126);
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        clearenv();
        setenv("PATH", "/usr/bin:/bin", 1);
        setenv("HOME", home.c_str(), 1);
        setenv("XDG_DATA_HOME", home.c_str(), 1);
        setenv("XDG_CONFIG_HOME", home.c_str(), 1);
        setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
        setenv("WAYLAND_DISPLAY", wayland_socket.c_str(), 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("TERM", "xterm-256color", 1);
        setenv("LEON_APP_ID", id.c_str(), 1);
        std::vector<char *> av;
        for (auto &a : args)
            av.push_back(a.data());
        av.push_back(nullptr);
        execv(exe.c_str(), av.data());
        _exit(127);
    }
    running[id] = pid;
    return pid;
}
int main(int argc, char **argv) {
    if (geteuid()) {
        std::cerr << "leon-launchd requires root\n";
        return 1;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    leon::Packages packages;
    std::string socket_name = "/run/leon/apps.sock";
    bool development = false;
    for (int i = 1; i < argc; ++i) {
        std::string option = argv[i];
        if (i + 1 == argc) {
            std::cerr << "Missing option value\n";
            return 2;
        }
        std::string value = argv[++i];
        if (option == "--root") {
            packages.root = value;
            development = true;
        } else if (option == "--builtin")
            packages.builtin = value;
        else if (option == "--socket")
            socket_name = value;
        else if (option == "--runtime-dir")
            runtime_prefix = value;
        else if (option == "--wayland")
            wayland_socket = value;
        else {
            std::cerr << "Unknown option: " << option << '\n';
            return 2;
        }
    }
    const char *socket_path = socket_name.c_str();
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_name.size() >= sizeof(address.sun_path))
        return 2;
    strcpy(address.sun_path, socket_path);
    unlink(socket_path);
    mode_t old = umask(0);
    if (bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ||
        listen(server, 16)) {
        perror(socket_path);
        return 1;
    }
    chmod(socket_path, 0666);
    umask(old);
    Json defaults{
        {"dark", false}, {"language", "zh-CN"}, {"navigation", "gestures"}, {"volume", 70}};
    auto prefs = packages.root / "settings.json";
    if (!fs::exists(prefs))
        leon::atomic_json(prefs, defaults);
    Json notifications = Json::array();
    while (!quitting) {
        reap();
        pollfd ready{server, POLLIN, 0};
        if (poll(&ready, 1, 200) <= 0)
            continue;
        int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        timeval timeout{1, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        Json response;
        try {
            ucred peer{};
            socklen_t len = sizeof(peer);
            fail(!getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &len),
                 "cannot identify client");
            std::string text;
            char ch;
            while (text.size() < 8192 && read(client, &ch, 1) == 1 && ch != '\n')
                text += ch;
            fail(text.size() < 8192, "request too large");
            auto request = Json::parse(text);
            auto op = request.at("op").get<std::string>();
            bool shell = peer.uid == 0 || peer.uid == 1000;
            bool settings = shell || peer.uid == 11000;
            if (op == "list") {
                fail(shell, "permission denied");
                response = {{"apps", packages.list()}, {"running", running}};
            } else if (op == "launch") {
                fail(shell, "permission denied");
                response = {{"pid", launch(packages, request.at("id"))}};
            } else if (op == "install") {
                fail(shell, "permission denied");
                packages.install(request.at("file").get<std::string>());
            } else if (op == "remove") {
                fail(shell, "permission denied");
                std::string id = request.at("id");
                if (running.contains(id)) {
                    kill(-running[id], SIGKILL);
                    running.erase(id);
                }
                packages.remove(id, request.value("purge", false));
            } else if (op == "stop") {
                fail(shell, "permission denied");
                std::string id = request.at("id");
                if (running.contains(id)) {
                    kill(-running[id], SIGKILL);
                    running.erase(id);
                }
            } else if (op == "settings")
                response = {{"settings", leon::read_json(prefs)}};
            else if (op == "set") {
                fail(settings, "permission denied");
                auto value = request.at("value");
                std::string key = request.at("key");
                bool valid = (key == "dark" && value.is_boolean()) ||
                             (key == "language" && (value == "zh-CN" || value == "en")) ||
                             (key == "navigation" && (value == "gestures" || value == "buttons")) ||
                             (key == "volume" && value.is_number_integer() &&
                              value.get<int>() >= 0 && value.get<int>() <= 100);
                fail(valid, "invalid setting");
                if (key == "volume") {
                    int percent = value;
                    fail(run_tool({"/usr/bin/wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                                   std::to_string(percent) + "%"}) == 0,
                         "audio sink unavailable");
                }
                auto state = leon::read_json(prefs);
                state[key] = value;
                leon::atomic_json(prefs, state);
                response = {{"settings", state}};
            } else if (op == "poweroff" || op == "reboot") {
                fail(shell, "permission denied");
                fail(!development, "power operations disabled for development roots");
                kill(1, op == "reboot" ? SIGUSR2 : SIGUSR1);
            } else if (op == "notifications") {
                fail(shell, "permission denied");
                response = {{"notifications", notifications}};
            } else if (op == "notify") {
                std::string id;
                for (const auto &m : packages.list())
                    if (m.at("uid").get<unsigned>() == peer.uid)
                        id = m.at("id");
                fail(!id.empty(), "unknown application identity");
                auto title = request.at("title").get<std::string>();
                fail(title.size() <= 160, "notification too long");
                notifications.push_back({{"app", id}, {"title", title}});
                if (notifications.size() > 64)
                    notifications.erase(notifications.begin());
            } else
                throw std::runtime_error("unknown operation");
            response["ok"] = true;
        } catch (const std::exception &e) {
            response = {{"ok", false}, {"error", e.what()}};
        }
        auto out = response.dump() + "\n";
        send(client, out.data(), out.size(), MSG_NOSIGNAL);
        close(client);
    }
    for (const auto &[id, pid] : running) {
        (void)id;
        kill(-pid, SIGKILL);
    }
    unlink(socket_path);
    close(server);
}
