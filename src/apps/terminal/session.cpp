#include "session.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
namespace leon::terminal {
Session::Session(Output output, std::vector<std::string> command)
    : output_(std::move(output)), command_(std::move(command)) {}
Session::~Session() { stop(); }
void Session::start(int rows, int columns) {
    if (pid_ || fd_ >= 0 || command_.empty())
        return;
    error_.clear();
    status_ = 0;
    winsize size{static_cast<unsigned short>(rows), static_cast<unsigned short>(columns), 0, 0};
    pid_t parent = getpid();
    pid_ = forkpty(&fd_, nullptr, nullptr, &size);
    if (!pid_) {
        if (prctl(PR_SET_PDEATHSIG, SIGHUP) || getppid() != parent)
            _exit(126);
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);
        for (int signal : {SIGPIPE, SIGTERM, SIGINT, SIGQUIT, SIGHUP, SIGCHLD})
            ::signal(signal, SIG_DFL);
        if (syscall(SYS_close_range, 3, ~0U, 0)) {
            rlimit limit{};
            getrlimit(RLIMIT_NOFILE, &limit);
            for (unsigned fd = 3; fd < std::min<rlim_t>(limit.rlim_cur, 1048576); ++fd)
                close(int(fd));
        }
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("SHELL", "/bin/sh", 1);
        setenv("LANG", "C.UTF-8", 1);
        unsetenv("COLUMNS");
        unsetenv("LINES");
        const char *home = getenv("HOME");
        if (home && chdir(home)) {
            perror("terminal home");
            _exit(126);
        }
        termios attributes{};
        if (!tcgetattr(STDIN_FILENO, &attributes)) {
#ifdef IUTF8
            attributes.c_iflag |= IUTF8;
#endif
            tcsetattr(STDIN_FILENO, TCSANOW, &attributes);
        }
        std::vector<char *> args;
        for (auto &arg : command_)
            args.push_back(arg.data());
        args.push_back(nullptr);
        execvp(args[0], args.data());
        perror("terminal exec");
        _exit(127);
    }
    if (pid_ < 0) {
        pid_ = 0;
        error_ = std::strerror(errno);
        close_pty();
        return;
    }
    if (fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0 || fcntl(fd_, F_SETFL, O_NONBLOCK) < 0) {
        error_ = std::strerror(errno);
        stop();
    }
}
void Session::resize(int rows, int columns) {
    if (fd_ < 0)
        return;
    winsize size{static_cast<unsigned short>(rows), static_cast<unsigned short>(columns), 0, 0};
    if (ioctl(fd_, TIOCSWINSZ, &size))
        error_ = std::strerror(errno);
}
void Session::close_pty() {
    if (fd_ >= 0)
        close(fd_);
    fd_ = -1;
    pending_.clear();
    sent_ = 0;
}
bool Session::send(std::string_view bytes) {
    if (fd_ < 0 || !pid_)
        return false;
    if (pending_.size() - sent_ + bytes.size() > 1024 * 1024) {
        error_ = "Terminal input queue is full";
        return false;
    }
    if (sent_) {
        pending_.erase(0, sent_);
        sent_ = 0;
    }
    pending_.append(bytes);
    flush();
    return true;
}
void Session::flush() {
    size_t budget = 0;
    while (fd_ >= 0 && sent_ < pending_.size() && budget < 65536) {
        ssize_t count = write(fd_, pending_.data() + sent_,
                              std::min<size_t>(65536 - budget, pending_.size() - sent_));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        if (count <= 0) {
            error_ = "PTY input closed";
            close_pty();
            return;
        }
        sent_ += size_t(count);
        budget += size_t(count);
    }
    if (sent_ == pending_.size()) {
        pending_.clear();
        sent_ = 0;
    }
}
bool Session::pump() {
    bool changed = false;
    char buffer[8192];
    for (size_t budget = 0; fd_ >= 0 && budget < 65536;) {
        ssize_t count = read(fd_, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (count <= 0) {
            if (count < 0 && errno != EIO)
                error_ = std::strerror(errno);
            close_pty();
            changed = true;
            break;
        }
        output_(std::string_view(buffer, size_t(count)));
        budget += size_t(count);
        changed = true;
    }
    flush();
    if (pid_ > 0) {
        int status;
        if (waitpid(pid_, &status, WNOHANG) == pid_) {
            status_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            pid_ = 0;
            changed = true;
        }
    }
    return changed;
}
void Session::stop() {
    if (pid_ > 0) {
        // The PTY foreground group can differ from the interactive shell's
        // group while vi/top or a pipeline owns job control.
        pid_t foreground = fd_ >= 0 ? tcgetpgrp(fd_) : -1;
        if (foreground > 1 && foreground != getpgrp())
            kill(-foreground, SIGHUP);
        kill(-pid_, SIGHUP);
        kill(pid_, SIGHUP);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        int status;
        while (std::chrono::steady_clock::now() < deadline) {
            if (waitpid(pid_, &status, WNOHANG) == pid_) {
                pid_ = 0;
                break;
            }
            usleep(2000);
        }
        if (foreground > 1 && foreground != getpgrp())
            kill(-foreground, SIGKILL);
        if (pid_ > 0) {
            kill(-pid_, SIGKILL);
            kill(pid_, SIGKILL);
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    pid_ = 0;
    close_pty();
}
} // namespace leon::terminal
