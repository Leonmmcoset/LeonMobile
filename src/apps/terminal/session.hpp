#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>
namespace leon::terminal {
class Session {
  public:
    using Output = std::function<void(std::string_view)>;
    explicit Session(Output output, std::vector<std::string> command = {"/bin/sh", "-i"});
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    void start(int rows, int columns);
    void resize(int rows, int columns);
    bool send(std::string_view bytes);
    bool pump();
    void stop();
    int fd() const { return fd_; }
    pid_t pid() const { return pid_; }
    bool running() const { return pid_ > 0; }
    bool wants_write() const { return !pending_.empty(); }
    int status() const { return status_; }
    const std::string &error() const { return error_; }

  private:
    int fd_{-1}, status_{};
    pid_t pid_{};
    Output output_;
    std::vector<std::string> command_;
    std::string pending_, error_;
    size_t sent_{};
    void flush();
    void close_pty();
};
} // namespace leon::terminal
