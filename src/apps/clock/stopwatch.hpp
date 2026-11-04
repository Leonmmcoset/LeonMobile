#pragma once
#include <chrono>
namespace leon::apps {
class Stopwatch {
  public:
    bool running() const { return running_; }
    double seconds() const;
    void toggle();
    void reset();
    void restore(double seconds);

  private:
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
    bool running_{};
    double elapsed_{};
};
} // namespace leon::apps
