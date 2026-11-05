#include "processes.hpp"
#include "internal.hpp"
#include "root.hpp"
#include "sandbox.h"
#include "namespace.h"
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <leon/ipc.hpp>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
namespace leon::appd {
static long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static void publish_exit(const std::string &id, pid_t pid, int status) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, "/run/leon/crash.sock");
    if (!connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))) {
        auto text =
            Json{{"op", "record_exit"}, {"app", id}, {"pid", pid}, {"status", status}}.dump() +
            "\n";
        (void)send(fd, text.data(), text.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    }
    close(fd);
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
    fail(clients_.at(id).stop_phase == StopPhase::none, "application is stopping");
    for (auto &[other, client] : clients_) {
        bool active = other == id;
        if (client.foreground != active) {
            priority(other, active);
            event(other, active ? "foreground" : "background");
        }
    }
}
void Processes::priority(const std::string &id, bool foreground) {
    clients_.at(id).foreground = foreground;
    if (!options_.development && clients_.at(id).process_fd >= 0) {
        int fd = openat(clients_.at(id).process_fd, "oom_score_adj", O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            const char *value = foreground ? "0" : "500";
            (void)write(fd, value, strlen(value));
            close(fd);
        }
    }
}
void Processes::background() {
    for (auto &[id, client] : clients_)
        if (client.foreground) {
            priority(id, false);
            event(id, "background");
        }
}
void Processes::save(const std::string &id) {
    fail(running_.contains(id), "application is not running");
    fail(clients_.at(id).stop_phase == StopPhase::none, "application is stopping");
    fail(clients_.at(id).fd >= 0, "application does not support lifecycle requests");
    event(id, "save");
}
Json Processes::lifecycle() const {
    Json states = Json::object();
    for (const auto &[id, client] : clients_) {
        const char *phase = client.stop_phase == StopPhase::shutdown ? "shutdown"
                            : client.stop_phase == StopPhase::term   ? "term"
                            : client.stop_phase == StopPhase::kill   ? "kill"
                                                                     : "running";
        states[id] = {{"pid", running_.at(id)},
                      {"foreground", client.foreground},
                      {"cooperative", client.fd >= 0},
                      {"responsive", client.responsive},
                      {"pending", client.pending},
                      {"event", client.event},
                      {"stopping", client.stop_phase != StopPhase::none},
                      {"stop_phase", phase}};
    }
    return {{"applications", states}, {"exits", exits_}};
}
std::string Processes::reclaim() {
    // Only background cooperative apps may be reclaimed automatically. A
    // foreground app or legacy process never becomes a pressure victim here.
    std::string victim;
    long long oldest = LLONG_MAX;
    for (const auto &[id, client] : clients_)
        if (!client.foreground && client.fd >= 0 && client.stop_phase == StopPhase::none &&
            client.deadline < oldest) {
            oldest = client.deadline;
            victim = id;
        }
    if (!victim.empty())
        stop(victim);
    return victim;
}
void Processes::reap() {
    for (auto i = retired_logs_.begin(); i != retired_logs_.end();) {
        leon_log_capture_drain(&i->capture, options_.log_socket.c_str(), i->uid,
                               i->pid, LEON_LOG_APP, i->id.c_str());
        if (!i->capture.count && !i->capture.lost)
            i = retired_logs_.erase(i);
        else
            ++i;
    }
    if (!options_.log_socket.empty())
        for (auto &[id, client] : clients_)
            leon_log_capture_drain(&client.log, options_.log_socket.c_str(), client.uid,
                                   running_.at(id), LEON_LOG_APP, id.c_str());
    poll_clients();
    std::map<pid_t, int> reaped;
    pid_t child;
    int child_status;
    while ((child = waitpid(-1, &child_status, WNOHANG)) > 0)
        reaped[child] = child_status;
    for (auto i = running_.begin(); i != running_.end();) {
        auto &client = clients_.at(i->first);
        pid_t p = i->second;
        int status = 125 << 8;
        bool done = false;
        if (client.namespace_fd >= 0) {
            int result = leon_namespace_receive(client.namespace_fd, LEON_NS_EXITED,
                                                 client.supervisor, client.uid, nullptr, &status);
            done = result == 1 || reaped.contains(client.supervisor);
            if (result < 0 && !reaped.contains(client.supervisor))
                kill(client.supervisor, SIGKILL);
            if (result != 1 && reaped.contains(client.supervisor)) {
                status = reaped.at(client.supervisor);
                if (WIFEXITED(status)) status = 125 << 8;
            }
        } else if (reaped.contains(p)) {
            status = reaped.at(p);
            done = true;
        }
        if (done) {
            if (options_.development)
                kill(-p, SIGKILL);
            if (!options_.log_socket.empty())
                leon_log_capture_close(&client.log, options_.log_socket.c_str(), client.uid,
                                       p, LEON_LOG_APP, i->first.c_str());
            if (client.log.count || client.log.lost) {
                if (retired_logs_.size() == 16) {
                    retired_logs_.erase(retired_logs_.begin());
                    std::fputs("application logging: retired offline buffer evicted\n", stderr);
                }
                retired_logs_.push_back({i->first, client.uid, p, client.log});
            }
            if (!options_.development && leon_cgroup_kill(client.uid))
                perror("application cgroup cleanup");
            if (client.namespace_fd >= 0) {
                close(client.namespace_fd);
                if (!reaped.contains(client.supervisor))
                    kill(client.supervisor, SIGKILL);
            }
            if (client.fd >= 0)
                close(client.fd);
            if (client.process_fd >= 0)
                close(client.process_fd);
            if (!options_.development)
                publish_exit(i->first, p,
                             WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
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
    advance_stops();
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
    std::string runtime = options_.runtime_prefix + "/" + std::to_string(uid);
    fs::create_directories(runtime);
    for (const auto &path : {home, fs::path(runtime)}) {
        int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        bool valid = fd >= 0 && !fchown(fd, uid, uid) && !fchmod(fd, 0700);
        if (fd >= 0) close(fd);
        fail(valid, "cannot prepare private application directory");
    }
    auto exe = fs::canonical(packages.executable(m));
    std::vector<std::string> args{exe.string()};
    for (const auto &a : m.value("args", Json::array()))
        args.push_back(a.get<std::string>());
    /* Pin both executable and working directory to the selected version. */
    auto work = m.value("builtin", false) ? packages.builtin.parent_path() : exe.parent_path();
    auto package = m.value("builtin", false) ? fs::path{} :
                   fs::canonical(packages.root / "apps" / id / "current");
    bool network = false;
    if (!options_.development && m.contains("permissions")) {
        for (const auto &permission : m.at("permissions"))
            if (permission == "network") {
                auto response = Json::parse(
                    leon::request("/run/leon/portal.sock",
                                  {{"op", "query"}, {"uid", uid}, {"permission", "network"}}));
                fail(response.value("ok", false), "permission service unavailable");
                network = response.value("granted", false);
            }
    }
    struct NamespaceChannel {
        int fd[2]{-1, -1};
        explicit NamespaceChannel(bool enabled) {
            if (!enabled) return;
            fail(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, fd),
                 "cannot create namespace control channel");
            int enabled_credentials = 1;
            if (setsockopt(fd[0], SOL_SOCKET, SO_PASSCRED, &enabled_credentials,
                           sizeof(enabled_credentials))) {
                close(fd[0]); close(fd[1]);
                fail(false, "cannot authenticate namespace identity");
            }
        }
        ~NamespaceChannel() {
            for (int value : fd) if (value >= 0) close(value);
        }
    } isolation(!options_.development);
    int gate[2];
    fail(!pipe2(gate, O_CLOEXEC), "cannot create launch gate");
    int executed[2];
    if (pipe2(executed, O_CLOEXEC)) {
        close(gate[0]);
        close(gate[1]);
        fail(false, "cannot create exec acknowledgement");
    }
    int pair[2] = {-1, -1};
    if (m.value("lifecycle", 0) == 1 &&
        socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair)) {
        close(gate[0]);
        close(gate[1]);
        close(executed[0]);
        close(executed[1]);
        fail(false, "cannot create application lifecycle channel");
    }
    pid_t parent = getpid();
    struct Output {
        leon_log_capture capture{};
        int write{-1};
        Output() { capture.fd = -1; }
        ~Output() {
            if (capture.fd >= 0) close(capture.fd);
            if (write >= 0) close(write);
        }
    } output;
    if (!options_.log_socket.empty() && (output.write = leon_log_capture_open(&output.capture)) < 0) {
        close(gate[0]); close(gate[1]);
        close(executed[0]); close(executed[1]);
        if (pair[0] >= 0) { close(pair[0]); close(pair[1]); }
        fail(false, "cannot create application log pipe");
    }
    pid_t pid = options_.development ? fork() : leon_namespace_clone();
    if (pid < 0) {
        close(executed[0]);
        close(executed[1]);
        close(gate[0]);
        close(gate[1]);
        if (pair[0] >= 0) {
            close(pair[0]);
            close(pair[1]);
        }
        fail(false, "cannot fork application");
    }
    if (!pid) {
        if (isolation.fd[0] >= 0) close(isolation.fd[0]);
        for (const auto &[other, client] : clients_)
            if (client.log.fd >= 0)
                close(client.log.fd);
        if (output.capture.fd >= 0)
            close(output.capture.fd);
        close(executed[0]);
        auto child_fail = [&](int status) {
            int cause = errno ? errno : EIO;
            (void)write(executed[1], &cause, sizeof(cause));
            _exit(status);
        };
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) ||
            getppid() != (options_.development ? parent : 0))
            child_fail(126);
        close(gate[1]);
        char go;
        if (read(gate[0], &go, 1) != 1 || go != '1')
            child_fail(126);
        close(gate[0]);
        if (pair[0] >= 0)
            close(pair[0]);
        if (!options_.development) {
            if (leon_namespace_filesystem(home.c_str(), runtime.c_str(), package.c_str(), uid) ||
                leon_namespace_fork(isolation.fd[1], executed[1], uid))
                child_fail(126);
            parent = 1;
        }
        sigset_t empty;
        sigemptyset(&empty);
        if (sigprocmask(SIG_SETMASK, &empty, nullptr))
            child_fail(126);
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        if (setsid() < 0)
            child_fail(126);
        umask(077);
        int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (input < 0)
            child_fail(126);
        if (input >= 0) {
            dup2(input, STDIN_FILENO);
            close(input);
        }
        int log = output.write;
        if (log < 0) {
            log = open((home / "application.log").c_str(),
                       O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_NONBLOCK, 0600);
            struct stat log_info {};
            if (log < 0 || fstat(log, &log_info) || !S_ISREG(log_info.st_mode) || fchown(log, uid, uid))
                child_fail(126);
        }
        if (log >= 0) {
            dup2(log, 1);
            dup2(log, 2);
            close(log);
        }
        if (setgroups(0, nullptr) || setgid(uid) || setuid(uid) || chdir(work.c_str()))
            child_fail(126);
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            child_fail(126);
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
        setenv("TMPDIR", options_.development ? (home / ".tmp").c_str() : "/tmp", 1);
        if (options_.development) mkdir((home / ".tmp").c_str(), 0700);
        int lifecycle_fd = -1;
        if (pair[1] >= 0) {
            int fd = fcntl(pair[1], F_DUPFD, 32);
            if (fd < 0)
                child_fail(126);
            close(pair[1]);
            lifecycle_fd = fd;
            setenv("LEON_LIFECYCLE_FD", std::to_string(fd).c_str(), 1);
        }
        std::vector<char *> av;
        for (auto &a : args)
            av.push_back(a.data());
        av.push_back(nullptr);
        if (!options_.development && leon_sandbox(home.c_str(), runtime.c_str(), network, 1)) {
            perror("application sandbox");
            child_fail(126);
        }
        if (!options_.development) {
            int keep[] = {executed[1], isolation.fd[1], lifecycle_fd};
            if (leon_namespace_close_fds(3, keep, 3) || leon_namespace_identify(isolation.fd[1]))
                child_fail(126);
            close(isolation.fd[1]);
        }
        execv(exe.c_str(), av.data());
        child_fail(127);
    }
    if (isolation.fd[1] >= 0) {
        close(isolation.fd[1]);
        isolation.fd[1] = -1;
    }
    if (output.write >= 0) {
        close(output.write);
        output.write = -1;
    }
    close(executed[1]);
    close(gate[0]);
    if (!options_.development && leon_cgroup(pid, uid)) {
        close(gate[1]);
        close(executed[0]);
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        if (pair[0] >= 0)
            close(pair[0]);
        if (pair[1] >= 0)
            close(pair[1]);
        fail(false, "cannot enforce application cgroup limits");
    }
    char go = '1';
    if (write(gate[1], &go, 1) != 1) {
        close(gate[1]);
        close(executed[0]);
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        if (pair[0] >= 0)
            close(pair[0]);
        if (pair[1] >= 0)
            close(pair[1]);
        fail(false, "application launch gate failed");
    }
    close(gate[1]);
    if (pair[1] >= 0)
        close(pair[1]);
    pollfd ready{executed[0], POLLIN | POLLHUP, 0};
    int cause = EIO;
    int polled;
    do {
        polled = poll(&ready, 1, 3000);
    } while (polled < 0 && errno == EINTR);
    ssize_t received = polled > 0 ? read(executed[0], &cause, sizeof(cause)) : -1;
    close(executed[0]);
    if (received != 0) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        if (pair[0] >= 0)
            close(pair[0]);
        throw std::runtime_error(std::string("application setup/exec failed: ") + strerror(cause));
    }
    pid_t application = pid;
    if (!options_.development &&
        (leon_namespace_receive(isolation.fd[0], LEON_NS_STARTED, 0, uid, &application, nullptr) != 1 ||
         application == pid)) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        if (pair[0] >= 0) close(pair[0]);
        fail(false, "cannot authenticate application namespace PID");
    }
    running_[id] = application;
    clients_[id].fd = pair[0];
    clients_[id].uid = uid;
    clients_[id].supervisor = pid;
    clients_[id].namespace_fd = isolation.fd[0];
    isolation.fd[0] = -1;
    if (!options_.development) {
        // Pin proc lookups to this process rather than a recyclable numeric
        // path. If it already exited, priority updates are unnecessary.
        auto path = "/proc/" + std::to_string(application);
        int fd = open(path.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
        struct stat actual {}, expected {};
        auto init_ns = "/proc/" + std::to_string(pid) + "/ns/pid";
        if (fd >= 0 && !fstatat(fd, "ns/pid", &actual, 0) &&
            !stat(init_ns.c_str(), &expected) && actual.st_ino == expected.st_ino &&
            actual.st_dev == expected.st_dev)
            clients_[id].process_fd = fd;
        else if (fd >= 0)
            close(fd);
    }
    clients_[id].log = output.capture;
    output.capture.fd = -1;
    clients_[id].root_helper = m.contains("root");
    foreground(id);
    return application;
}
void Processes::request_stop(const std::string &id) {
    if (!running_.contains(id))
        return;
    auto &client = clients_.at(id);
    // Repeated taps must not extend the checkpoint or termination deadline.
    if (client.stop_phase != StopPhase::none)
        return;
    if (client.root_helper) {
        try {
            revoke_root(options_, id);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Root cancellation: %s\n", e.what());
        }
    }
    priority(id, false);
    event(id, "shutdown");
    client.stop_phase = StopPhase::shutdown;
    client.stop_deadline = now_ms() + (client.fd >= 0 ? 2000 : 0);
    advance_stops();
}
void Processes::advance_stops() {
    auto now = now_ms();
    for (auto &[id, client] : clients_) {
        if (client.stop_phase == StopPhase::none || now < client.stop_deadline)
            continue;
        pid_t pid = running_.at(id);
        if (client.stop_phase == StopPhase::shutdown) {
            if (client.namespace_fd >= 0)
                leon_namespace_signal(client.namespace_fd, SIGTERM);
            else
                kill(-pid, SIGTERM);
            client.stop_phase = StopPhase::term;
            client.stop_deadline = now + 1000;
        } else {
            if (!options_.development && leon_cgroup_kill(client.uid))
                perror("application cgroup termination");
            if (client.namespace_fd >= 0)
                kill(client.supervisor, SIGKILL);
            else
                kill(-pid, SIGKILL);
            // Reaping or the authenticated PID 1 report retires the task.
            client.stop_phase = StopPhase::kill;
            client.stop_deadline = now + 1000;
        }
    }
}
void Processes::stop(const std::string &id) {
    request_stop(id);
    auto deadline = now_ms() + 4000;
    while (running_.contains(id) && now_ms() < deadline) {
        reap();
        if (running_.contains(id))
            usleep(10000);
    }
    fail(!running_.contains(id), "application is still stopping");
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
        if (!options_.development)
            leon_cgroup_kill(clients_.at(id).uid);
        auto &client = clients_.at(id);
        if (client.namespace_fd >= 0) {
            kill(client.supervisor, SIGKILL);
            close(client.namespace_fd);
        } else
            kill(-pid, SIGKILL);
        if (clients_.at(id).fd >= 0)
            close(clients_.at(id).fd);
        if (client.process_fd >= 0)
            close(client.process_fd);
        if (!options_.log_socket.empty())
            leon_log_capture_close(&clients_.at(id).log, options_.log_socket.c_str(),
                                   clients_.at(id).uid, pid, LEON_LOG_APP, id.c_str());
    }
}
} // namespace leon::appd
