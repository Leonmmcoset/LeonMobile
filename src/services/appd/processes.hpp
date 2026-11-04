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
    void request_stop(const std::string &id);
    void stop(const std::string &id);
    const std::map<std::string, pid_t> &running() const { return running_; }
    void foreground(const std::string &id);
    void background();
    void save(const std::string &id);
    Json lifecycle() const;
    std::string reclaim();

  private:
    Options options_;
    std::map<std::string, pid_t> running_;
    enum class StopPhase { none, shutdown, term, kill };
    struct Client {
        uid_t uid{};
        int fd{-1};
        unsigned sequence{};
        long long deadline{};
        long long stop_deadline{};
        StopPhase stop_phase{StopPhase::none};
        bool foreground{}, pending{}, responsive{true};
        std::string event;
    };
    std::map<std::string, Client> clients_;
    Json exits_ = Json::array();
    void event(const std::string &id, const std::string &name);
    void priority(const std::string &id, bool foreground);
    void poll_clients();
    void advance_stops();
};
} // namespace leon::appd
