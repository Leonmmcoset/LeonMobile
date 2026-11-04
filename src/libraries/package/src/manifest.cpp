#include "internal.hpp"
#include <regex>
#include <set>
#include <sstream>
namespace leon::detail {
std::array<unsigned, 3> version(const std::string &s) {
    require(std::regex_match(
                s, std::regex("(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})")),
            "version must be numeric major.minor.patch");
    std::array<unsigned, 3> v;
    char dot;
    std::istringstream in(s);
    in >> v[0] >> dot >> v[1] >> dot >> v[2];
    return v;
}
bool safe_path(const std::string &s) {
    if (s.empty() || s[0] == '/' || s.find('\\') != std::string::npos || s.size() > 240)
        return false;
    for (const auto &p : fs::path(s))
        if (p == ".." || p == "." || p.empty())
            return false;
    return true;
}
} // namespace leon::detail
namespace leon {
using namespace detail;
bool valid_id(const std::string &id) {
    return id.size() <= 96 &&
           std::regex_match(id, std::regex("[a-z][a-z0-9]*(\\.[a-z][a-z0-9_-]*)+"));
}
void validate_manifest(const Json &m, bool builtin) {
    require(m.is_object() && m.value("format", 0) == 1, "unsupported application format");
    require(valid_id(m.at("id").get<std::string>()), "invalid application ID");
    version(m.at("version").get<std::string>());
    require(m.at("arch") == "aarch64", "application architecture must be aarch64");
    require(m.value("lmui", 0) == 1, "unsupported LMUI ABI");
    if (m.contains("lifecycle"))
        require(m.at("lifecycle").is_number_integer() && m.at("lifecycle") == 1,
                "unsupported application lifecycle protocol");
    if (m.contains("permissions")) {
        require(m["permissions"].is_array() && m["permissions"].size() <= 2, "invalid permissions");
        std::set<std::string> seen;
        for (const auto &permission : m["permissions"]) {
            require(permission.is_string(), "invalid permission");
            auto value = permission.get<std::string>();
            require((value == "network" || value == "files.import") && seen.insert(value).second,
                    "unknown or duplicate permission");
        }
    }
    require(m.at("name").is_string() && !m.at("name").get<std::string>().empty(),
            "missing application name");
    std::string entry = m.at("entry");
    require(builtin ? entry.starts_with("/usr/bin/") : safe_path(entry), "invalid executable path");
    if (m.contains("args")) {
        require(m["args"].is_array() && m["args"].size() <= 16, "invalid arguments");
        for (const auto &a : m["args"])
            require(a.is_string(), "invalid argument");
    }
}
} // namespace leon
