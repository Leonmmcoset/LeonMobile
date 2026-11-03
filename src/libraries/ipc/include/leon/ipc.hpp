#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace leon {
using Json = nlohmann::json;
// Version 1 local JSON transport. Server authorization uses SO_PEERCRED.
std::string request(const std::string &socket, const Json &message);
} // namespace leon
