#pragma once
#include "options.hpp"
#include "processes.hpp"
#include "settings.hpp"
namespace leon::appd {
class Broker {
  public:
    explicit Broker(const Options &options);
    Json dispatch(const Json &request, uid_t caller);
    void reap() { processes_.reap(); }

  private:
    Options options_;
    Packages packages_;
    Processes processes_;
    Settings settings_;
};
int serve(Broker &broker, const std::string &socket_name);
} // namespace leon::appd
