#pragma once
#include <functional>
#include <stop_token>
#include <string>
#include <vector>
namespace leon {
enum class RootPhase { pending, running };
struct RootResult {
    int status{-1};
    bool cancelled{};
    std::string output, error;
    bool ok() const { return error.empty() && !cancelled && status >= 0; }
};
// Blocking: use a worker thread. The system UI receives the Root password.
// Cancelling or destroying the requesting connection terminates the helper.
RootResult request_root(const std::vector<std::string> &arguments, std::stop_token stop = {},
                        std::function<void(RootPhase)> phase = {});
} // namespace leon
