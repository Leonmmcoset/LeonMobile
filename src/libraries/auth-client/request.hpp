#pragma once
#include "auth.h"
#include "wire.h"
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>

namespace leon {
// Internal system UI adapter. Erase credentials in JSON, wire and reply buffers.
inline auth_state device_request(nlohmann::json request, unsigned timeout_ms = 400) {
    struct Clear {
        nlohmann::json &request;
        std::string wire;
        char response[LEON_AUTH_LIMIT]{};
        ~Clear() {
            for (const char *name : {"secret", "new_secret"}) {
                auto it = request.find(name);
                if (it != request.end() && it->is_string()) {
                    auto &s = it->get_ref<std::string &>();
                    root_clear(s.data(), s.size());
                }
            }
            root_clear(wire.data(), wire.size());
            root_clear(response, sizeof(response));
        }
    } clear{request, {}, {}};
    auth_state state{};
    state.locked = true;
    try {
        clear.wire = request.dump() + "\n";
        const char *endpoint = std::getenv("LEON_AUTH_SOCKET");
        if (!auth_exchange(endpoint ? endpoint : LEON_AUTH_SOCKET, clear.wire.c_str(),
                           clear.response, timeout_ms) &&
            !auth_decode(clear.response, &state))
            return state;
    } catch (...) {
    }
    state = {};
    state.locked = true;
    strcpy(state.error, "Authentication service unavailable");
    return state;
}
inline void erase_secret(std::string &secret) {
    root_clear(secret.data(), secret.size());
    secret.clear();
}
} // namespace leon
