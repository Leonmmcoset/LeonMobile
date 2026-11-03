#include "processes.hpp"
#include "internal.hpp"
#include <csignal>
#include <fcntl.h>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace leon::appd {
void Processes::reap() {
    pid_t p;
    while ((p = waitpid(-1, nullptr, WNOHANG)) > 0) {
        for (auto i = running_.begin(); i != running_.end();)
            if (i->second == p) {
                kill(-p, SIGKILL);
                i = running_.erase(i);
            } else
                ++i;
    }
}
pid_t Processes::launch(Packages &packages, const std::string &id) {
    reap();
    if (running_.contains(id))
        return running_[id];
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
    pid_t parent = getpid();
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
        std::vector<char *> av;
        for (auto &a : args)
            av.push_back(a.data());
        av.push_back(nullptr);
        execv(exe.c_str(), av.data());
        _exit(127);
    }
    running_[id] = pid;
    return pid;
}
void Processes::stop(const std::string &id) {
    if (running_.contains(id)) {
        kill(-running_[id], SIGKILL);
        running_.erase(id);
    }
}
Processes::~Processes() {
    for (const auto &[id, pid] : running_) {
        (void)id;
        kill(-pid, SIGKILL);
    }
}
} // namespace leon::appd
