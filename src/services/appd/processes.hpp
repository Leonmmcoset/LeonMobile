#pragma once
#include "options.hpp"
#include <leon/package.hpp>
#include <map>
#include <sys/types.h>
namespace leon::appd {
class Processes {
  public:
    explicit Processes(const Options &options) : options_(options) {}
    ~Processes();
    Processes(const Processes &) = delete;
    Processes &operator=(const Processes &) = delete;
    pid_t launch(Packages &packages, const std::string &id);
    void reap();
    void stop(const std::string &id);
    const std::map<std::string, pid_t> &running() const { return running_; }

  private:
    Options options_;
    std::map<std::string, pid_t> running_;
};
} // namespace leon::appd
