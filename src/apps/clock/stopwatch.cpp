#include "stopwatch.hpp"
#include <algorithm>
#include <cmath>
namespace leon::apps {
double Stopwatch::seconds() const {
    return elapsed_ +
           (running_
                ? std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count()
                : 0);
}
void Stopwatch::toggle() {
    if (running_)
        elapsed_ = seconds();
    else
        start_ = std::chrono::steady_clock::now();
    running_ = !running_;
}
void Stopwatch::reset() {
    elapsed_ = 0;
    start_ = std::chrono::steady_clock::now();
}
void Stopwatch::restore(double seconds) {
    elapsed_ = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 315360000.0) : 0;
    running_ = false;
}
} // namespace leon::apps
