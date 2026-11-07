#include "installer.hpp"
#include "internal.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <linux/memfd.h>
#include "../common/linux_openat2.h"
#include <openssl/rand.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace leon::appd {
namespace {
long long now() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}
void close_fd(int &fd) { if (fd >= 0) close(fd); fd = -1; }
void write_bytes(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t count = write(fd, data, size);
        if (count < 0 && errno == EINTR) continue;
        fail(count > 0, "package snapshot write failed");
        data += count; size -= size_t(count);
    }
}
std::string ticket() {
    unsigned char bytes[16];
    fail(RAND_bytes(bytes, sizeof(bytes)) == 1, "installation entropy unavailable");
    std::string out;
    for (auto b : bytes) { out += "0123456789abcdef"[b >> 4]; out += "0123456789abcdef"[b & 15]; }
    return out;
}
} // namespace
Installer::~Installer() { clear(); }
void Installer::clear() {
    if (worker_ >= 0) syscall(SYS_pidfd_send_signal, worker_, SIGKILL, nullptr, 0);
    close_fd(worker_); close_fd(pipe_); close_fd(snapshot_);
    token_.clear(); state_ = "idle"; output_.clear(); error_.clear(); review_ = nullptr;
}
void Installer::snapshot() {
    auto home = options_.root / "data/org.leon.installer";
    int dir = open(home.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    fail(dir >= 0, "installer private directory unavailable");
    open_how how{};
    how.flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    int source = int(syscall(SYS_openat2, dir, "incoming.lmpkg", &how, sizeof(how)));
    close(dir);
    fail(source >= 0, "selected package unavailable or unsafe");
    try {
        struct stat st{};
        fail(!fstat(source, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 &&
                 st.st_uid == 11008 && st.st_size > 0 && st.st_size <= 16 * 1024 * 1024,
             "package must be an installer-owned regular file of at most 16 MiB");
        snapshot_ = int(syscall(SYS_memfd_create, "leon-package", MFD_CLOEXEC | MFD_ALLOW_SEALING));
        fail(snapshot_ >= 0, "cannot allocate package snapshot");
        std::array<char, 65536> buffer;
        off_t remaining = st.st_size;
        while (remaining) {
            ssize_t n = read(source, buffer.data(), std::min<off_t>(remaining, buffer.size()));
            if (n < 0 && errno == EINTR) continue;
            fail(n > 0, "selected package changed during snapshot");
            write_bytes(snapshot_, buffer.data(), size_t(n)); remaining -= n;
        }
        fail(!fcntl(snapshot_, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL),
             "cannot seal package snapshot");
        close(source);
    } catch (...) { close(source); close_fd(snapshot_); throw; }
}
void Installer::start(bool install) {
    int channel[2];
    fail(!pipe2(channel, O_CLOEXEC), "cannot create package worker channel");
    auto parent = getpid();
    pid_t child = fork();
    if (child < 0) { close(channel[0]); close(channel[1]); throw std::runtime_error("cannot start package worker"); }
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(125);
        // Only the snapshot and result channel cross this boundary. The worker
        // cannot keep unrelated service connections or application PTYs alive.
        int output = fcntl(channel[1], F_DUPFD_CLOEXEC, 10);
        int archive = fcntl(snapshot_, F_DUPFD_CLOEXEC, 10);
        if (output < 0 || archive < 0 || dup2(output, 3) < 0 || dup2(archive, 4) < 0) _exit(125);
        if (syscall(SYS_close_range, 5u, ~0u, 0)) _exit(125);
        rlimit memory{768 * 1024 * 1024, 768 * 1024 * 1024}, cpu{60, 60};
        if (setrlimit(RLIMIT_AS, &memory) || setrlimit(RLIMIT_CPU, &cpu) ||
            prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) _exit(125);
        Json result;
        try {
            Packages packages(options_.root, options_.builtin);
            if (install) {
                packages.install_reviewed("/proc/self/fd/4", review_);
                result = {{"ok", true}};
            } else result = {{"ok", true}, {"review", packages.inspect("/proc/self/fd/4")}};
        } catch (const std::exception &e) { result = {{"ok", false}, {"error", e.what()}}; }
        auto text = result.dump();
        try { if (text.size() <= 16384) write_bytes(3, text.data(), text.size()); } catch (...) {}
        _exit(0);
    }
    close(channel[1]);
    worker_ = int(syscall(SYS_pidfd_open, child, 0));
    if (worker_ < 0) {
        kill(child, SIGKILL); waitpid(child, nullptr, 0); close(channel[0]);
        throw std::runtime_error("cannot supervise package worker");
    }
    pipe_ = channel[0];
    fcntl(pipe_, F_SETFL, O_NONBLOCK);
    output_.clear(); error_.clear();
    state_ = install ? "installing" : "checking";
    deadline_ = now() + 90000;
}
void Installer::poll() {
    if (!token_.empty() && now() >= expires_ && state_ != "installing") { clear(); return; }
    if (pipe_ < 0) return;
    char data[4096];
    bool eof = false;
    for (;;) {
        ssize_t n = read(pipe_, data, sizeof(data));
        if (n > 0) output_.append(data, size_t(n));
        else if (!n) { eof = true; break; }
        else if (errno == EINTR) continue;
        else if (errno == EAGAIN) break;
        else { eof = true; break; }
        if (output_.size() > 16384) break;
    }
    if (!eof && now() < deadline_ && output_.size() <= 16384) return;
    bool was_install = state_ == "installing";
    if (!eof || output_.size() > 16384) {
        syscall(SYS_pidfd_send_signal, worker_, SIGKILL, nullptr, 0);
        error_ = "package worker exceeded its limits; check installed version before retrying";
        state_ = "failed";
    } else {
        try {
            auto result = Json::parse(output_);
            if (!result.value("ok", false)) throw std::runtime_error(result.value("error", "package operation failed"));
            if (was_install) state_ = "installed";
            else { review_ = result.at("review"); state_ = "ready"; }
        } catch (const std::exception &e) { state_ = "failed"; error_ = e.what(); }
    }
    close_fd(pipe_); close_fd(worker_); output_.clear();
    if (state_ != "ready") close_fd(snapshot_);
}
Json Installer::status() const {
    Json out = {{"state", state_}, {"token", token_}};
    if (!review_.is_null()) out["review"] = review_;
    if (!error_.empty()) out["error"] = error_;
    return out;
}
Json Installer::dispatch(const Json &r, uid_t uid) {
    fail(uid == 0 || uid == 11008, "only the system installer may review or confirm packages");
    poll();
    auto op = r.at("op").get<std::string>();
    if (op == "package_prepare") {
        fail(state_ != "checking" && state_ != "ready" && state_ != "installing", "another package review is active");
        clear();
        snapshot();
        token_ = ticket(); expires_ = now() + 600000;
        try { start(false); } catch (...) { clear(); throw; }
    } else {
        if (r.contains("token")) fail(!token_.empty() && r.at("token") == token_, "package review expired or replaced");
        if (op == "package_commit") {
            fail(r.contains("token") && state_ == "ready" && review_.at("installable") == true,
                 "a valid, installable review must be confirmed first");
            start(true);
        } else if (op == "package_cancel") {
            fail(state_ != "installing", "installation is already committing; wait for its result");
            clear();
        } else fail(op == "package_status", "unknown package operation");
    }
    return status();
}
} // namespace leon::appd
