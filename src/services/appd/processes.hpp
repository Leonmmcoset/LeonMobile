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
    void foreground(const std::string &id);
    void background();
    Json lifecycle() const;
    std::string reclaim();

  private:
    Options options_;
    std::map<std::string, pid_t> running_;
    struct Client {
        int fd{-1};
        unsigned sequence{};
        long long deadline{};
        bool foreground{}, pending{}, responsive{true};
        std::string event;
    };
    std::map<std::string, Client> clients_;
    Json exits_ = Json::array();
    void event(const std::string &id, const std::string &name);
    void poll_clients();
};
} // namespace leon::appd
