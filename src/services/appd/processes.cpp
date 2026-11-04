#include "processes.hpp"
#include "internal.hpp"
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace leon::appd {
static long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
void Processes::event(const std::string &id, const std::string &name) {
    auto &client = clients_.at(id);
    client.event = name;
    if (client.fd < 0)
        return;
    auto packet = std::to_string(++client.sequence) + " " + name;
    client.pending = true;
    client.deadline = now_ms() + 2000;
    if (send(client.fd, packet.data(), packet.size(), MSG_DONTWAIT | MSG_NOSIGNAL) !=
        static_cast<ssize_t>(packet.size())) {
        client.pending = false;
        client.responsive = false;
    }
}
void Processes::poll_clients() {
    for (auto &[id, client] : clients_) {
        if (client.fd < 0)
            continue;
        char packet[64];
        for (int i = 0; i < 8; ++i) {
            ssize_t count = recv(client.fd, packet, sizeof(packet) - 1, MSG_DONTWAIT);
            if (count <= 0)
                break;
            packet[count] = 0;
            unsigned sequence;
            char result[8], tail;
            if (std::sscanf(packet, "%u %7s %c", &sequence, result, &tail) == 2 && client.pending &&
                sequence == client.sequence) {
                client.pending = false;
                client.responsive = !std::strcmp(result, "ok");
            }
        }
        if (client.pending && now_ms() >= client.deadline) {
            client.pending = false;
            client.responsive = false;
        }
    }
}
void Processes::foreground(const std::string &id) {
    fail(running_.contains(id), "application is not running");
    for (auto &[other, client] : clients_) {
        bool active = other == id;
        if (client.foreground != active) {
            client.foreground = active;
            event(other, active ? "foreground" : "background");
            if (!options_.development) {
                auto oom = "/proc/" + std::to_string(running_.at(other)) + "/oom_score_adj";
                int fd = open(oom.c_str(), O_WRONLY | O_CLOEXEC);
                if (fd >= 0) { const char *value = active ? "0" : "500"; write(fd, value, strlen(value)); close(fd); }
            }
        }
    }
}
void Processes::background() {
    for (auto &[id, client] : clients_)
        if (client.foreground) {
            client.foreground = false;
            event(id, "background");
            if (!options_.development) {
                auto path = "/proc/" + std::to_string(running_.at(id)) + "/oom_score_adj";
                int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
                if (fd >= 0) { write(fd, "500", 3); close(fd); }
            }
        }
}
Json Processes::lifecycle() const {
    Json states = Json::object();
    for (const auto &[id, client] : clients_)
        states[id] = {{"pid", running_.at(id)},        {"foreground", client.foreground},
                      {"cooperative", client.fd >= 0}, {"responsive", client.responsive},
                      {"pending", client.pending},     {"event", client.event}};
    return {{"applications", states}, {"exits", exits_}};
}
std::string Processes::reclaim() {
    // Only background cooperative apps may be reclaimed automatically. A
    // foreground app or legacy process never becomes a pressure victim here.
    std::string victim;
    long long oldest = LLONG_MAX;
    for (const auto &[id, client] : clients_)
        if (!client.foreground && client.fd >= 0 && client.deadline < oldest) {
            oldest = client.deadline; victim = id;
        }
    if (!victim.empty()) stop(victim);
    return victim;
}
void Processes::reap() {
    poll_clients();
    pid_t p;
    int status;
    while ((p = waitpid(-1, &status, WNOHANG)) > 0) {
        for (auto i = running_.begin(); i != running_.end();)
            if (i->second == p) {
                kill(-p, SIGKILL);
                auto &client = clients_.at(i->first);
                if (client.fd >= 0)
                    close(client.fd);
                exits_.push_back(
                    {{"app", i->first},
                     {"pid", p},
                     {"status", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)}});
                if (exits_.size() > 64)
                    exits_.erase(exits_.begin());
                clients_.erase(i->first);
                i = running_.erase(i);
            } else
                ++i;
    }
}
pid_t Processes::launch(Packages &packages, const std::string &id) {
    reap();
    if (running_.contains(id)) {
        foreground(id);
        return running_[id];
    }
    auto m = packages.lookup(id);
    unsigned uid = m.at("uid");
    fs::path home = packages.root / "data" / id;
    fs::create_directories(home);
    fail(!chown(home.c_str(), uid, uid) && !chmod(home.c_str(), 0700),
         "cannot create application home");
    std::string runtime = options_.runtime_prefix + "/" + std::to_string(uid);
    fs::create_directories(runtime);
    fail(!chown(runtime.c_str(), uid, uid) && !chmod(runtime.c_str(), 0700),
         "cannot create runtime directory");
    auto exe = fs::canonical(packages.executable(m));
    std::vector<std::string> args{exe.string()};
    for (const auto &a : m.value("args", Json::array()))
        args.push_back(a.get<std::string>());
    /* Pin both executable and working directory to the selected version. */
    auto work = m.value("builtin", false) ? packages.builtin.parent_path() : exe.parent_path();
    int pair[2] = {-1, -1};
    if (m.value("lifecycle", 0) == 1)
        fail(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair),
             "cannot create application lifecycle channel");
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        if (pair[0] >= 0) {
            close(pair[0]);
            close(pair[1]);
        }
        fail(false, "cannot fork application");
    }
    if (!pid) {
        if (pair[0] >= 0)
            close(pair[0]);
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
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(126);
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        clearenv();
        setenv("PATH", "/usr/bin:/bin", 1);
        setenv("HOME", home.c_str(), 1);
        setenv("XDG_DATA_HOME", home.c_str(), 1);
        setenv("XDG_CONFIG_HOME", home.c_str(), 1);
        setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
        setenv("WAYLAND_DISPLAY", options_.wayland_socket.c_str(), 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("TERM", "xterm-256color", 1);
        setenv("LEON_APP_ID", id.c_str(), 1);
        if (pair[1] >= 0) {
            int fd = fcntl(pair[1], F_DUPFD, 32);
            if (fd < 0)
                _exit(126);
            close(pair[1]);
            setenv("LEON_LIFECYCLE_FD", std::to_string(fd).c_str(), 1);
        }
        std::vector<char *> av;
        for (auto &a : args)
            av.push_back(a.data());
        av.push_back(nullptr);
        execv(exe.c_str(), av.data());
        _exit(127);
    }
    if (pair[1] >= 0)
        close(pair[1]);
    running_[id] = pid;
    clients_[id].fd = pair[0];
    foreground(id);
    return pid;
}
void Processes::stop(const std::string &id) {
    if (running_.contains(id)) {
        pid_t pid = running_.at(id);
        event(id, "shutdown");
        auto deadline = now_ms() + (clients_.at(id).fd >= 0 ? 2000 : 0);
        while (running_.contains(id) && now_ms() < deadline) {
            reap();
            usleep(10000);
        }
        if (running_.contains(id)) {
            kill(-pid, SIGTERM);
            deadline = now_ms() + 1000;
            while (running_.contains(id) && now_ms() < deadline) {
                reap();
                usleep(10000);
            }
        }
        if (running_.contains(id)) {
            kill(-pid, SIGKILL);
            int status;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            auto &client = clients_.at(id);
            if (client.fd >= 0)
                close(client.fd);
            clients_.erase(id);
            running_.erase(id);
        }
    }
}
Processes::~Processes() {
    for (const auto &[id, pid] : running_) {
        (void)pid;
        event(id, "shutdown");
    }
    auto deadline = now_ms() + 2000;
    while (!running_.empty() && now_ms() < deadline) {
        reap();
        usleep(10000);
    }
    for (const auto &[id, pid] : running_) {
        kill(-pid, SIGKILL);
        if (clients_.at(id).fd >= 0)
            close(clients_.at(id).fd);
    }
}
} // namespace leon::appd
