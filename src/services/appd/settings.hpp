#pragma once
#include "options.hpp"
#include <leon/package.hpp>
namespace leon::appd {
class Settings {
  public:
    explicit Settings(const Options &options);
    Json read() const;
    Json set(const Json &request);

  private:
    fs::path path_;
    std::string socket_;
};
} // namespace leon::appd
