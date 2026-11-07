#pragma once
#include "options.hpp"
#include <leon/package.hpp>

namespace leon::appd {
// One bounded review, owned exclusively by the trusted installer UI. Archive
// work runs in a separate process; apps.sock continues serving lifecycle IPC.
class Installer {
  public:
    explicit Installer(const Options &options) : options_(options) {}
    ~Installer();
    Json dispatch(const Json &request, uid_t caller);
    void poll();
    bool installing() const { return state_ == "installing"; }

  private:
    Options options_;
    std::string token_, state_{"idle"}, output_, error_;
    Json review_;
    int snapshot_{-1}, pipe_{-1}, worker_{-1};
    long long expires_{}, deadline_{};
    void clear();
    void start(bool install);
    void snapshot();
    Json status() const;
};
} // namespace leon::appd
