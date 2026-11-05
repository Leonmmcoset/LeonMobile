#pragma once
#include "wire.h"
#include <cstdlib>
#include <nlohmann/json.hpp>
namespace leon {
// Internal trusted UI adapter; this is deliberately absent from the application SDK.
inline nlohmann::json root_control(nlohmann::json request, unsigned timeout_ms = 400) {
    struct Secrets {
        nlohmann::json &request;
        std::string wire;
        ~Secrets() {
            root_clear(wire.data(), wire.size());
            for (const auto *name : {"password", "new_password"})
                if (request.contains(name) && request[name].is_string()) {
                    auto &s = request[name].get_ref<std::string &>();
                    root_clear(s.data(), s.size());
                }
        }
    } secrets{request, {}};
    try {
        secrets.wire = request.dump() + "\n";
        char response[LEON_ROOT_MESSAGE];
        const char *socket = std::getenv("LEON_ROOT_SOCKET");
        if (secrets.wire.size() < LEON_ROOT_MESSAGE &&
            !root_exchange(socket ? socket : LEON_ROOT_SOCKET, secrets.wire.c_str(), response,
                           sizeof(response), timeout_ms))
            return nlohmann::json::parse(response);
    } catch (...) {
    }
    return {{"ok", false}, {"error", "Root service unavailable; retry"}};
}
} // namespace leon
