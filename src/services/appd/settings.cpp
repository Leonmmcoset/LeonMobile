#include "settings.hpp"
#include "internal.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
namespace leon::appd {
static int run_tool(const std::vector<std::string> &args, const std::string &runtime_prefix) {
    pid_t parent = getpid();
    pid_t pid = fork();
    if (!pid) {
        setsid();
        if (setgroups(0, nullptr) || setgid(29) || setuid(1000))
            _exit(126);
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
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
Settings::Settings(const Options &options)
    : path_(options.root / "settings.json"), runtime_prefix_(options.runtime_prefix) {
    Json defaults{{"dark", false},
                  {"language", "zh-CN"},
                  {"navigation", "gestures"},
                  {"volume", 70},
                  {"reduce_motion", false}};
    if (!fs::exists(path_))
        atomic_json(path_, defaults);
}
Json Settings::read() const {
    auto state = read_json(path_);
    if (!state.contains("reduce_motion"))
        state["reduce_motion"] = false;
    return state;
}
Json Settings::set(const Json &request) {
    auto value = request.at("value");
    std::string key = request.at("key");
    bool valid = ((key == "dark" || key == "reduce_motion") && value.is_boolean()) ||
                 (key == "language" && (value == "zh-CN" || value == "en")) ||
                 (key == "navigation" && (value == "gestures" || value == "buttons")) ||
                 (key == "volume" && value.is_number_integer() && value.get<int>() >= 0 &&
                  value.get<int>() <= 100);
    fail(valid, "invalid setting");
    if (key == "volume") {
        int percent = value;
        fail(run_tool({"/usr/bin/wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                       std::to_string(percent) + "%"},
                      runtime_prefix_) == 0,
             "audio sink unavailable");
    }
    auto state = read();
    state[key] = value;
    leon::atomic_json(path_, state);
    return state;
}
} // namespace leon::appd
