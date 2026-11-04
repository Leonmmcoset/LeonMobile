#include "auth.h"
#include <cstring>
#include <nlohmann/json.hpp>
extern "C" int auth_decode(const char *text, struct auth_state *state) {
    *state = {};
    try {
        auto j = nlohmann::json::parse(text);
        state->ok = j.at("ok").get<bool>();
        state->locked = j.value("locked", true);
        state->configured = j.value("configured", false);
        state->trusted = j.value("trusted", false);
        state->generation = j.value("generation", int64_t(0));
        state->retry_after_ms = j.value("retry_after_ms", int64_t(0));
        for (auto pair : {std::pair{"epoch", state->epoch},
                          {"kind", state->kind},
                          {"token", state->token},
                          {"error", state->error}}) {
            auto value = j.value(pair.first, std::string{});
            size_t limit = !strcmp(pair.first, "epoch")   ? sizeof(state->epoch)
                           : !strcmp(pair.first, "kind")  ? sizeof(state->kind)
                           : !strcmp(pair.first, "token") ? sizeof(state->token)
                                                          : sizeof(state->error);
            if (value.size() >= limit || value.find('\0') != std::string::npos)
                return -1;
            memcpy(pair.second, value.c_str(), value.size() + 1);
        }
        return 0;
    } catch (...) {
        return -1;
    }
}
