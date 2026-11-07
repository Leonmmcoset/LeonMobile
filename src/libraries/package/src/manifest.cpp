#include "internal.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <sstream>
namespace leon::detail {
std::array<unsigned, 3> version(const std::string &s) {
    static const std::regex pattern("(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})");
    require(std::regex_match(s, pattern),
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
    static const std::regex pattern("[a-z][a-z0-9]*(\\.[a-z][a-z0-9_-]*)+");
    return id.size() <= 96 &&
           std::regex_match(id, pattern);
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
        require(m["permissions"].is_array() && m["permissions"].size() <= 3, "invalid permissions");
        std::set<std::string> seen;
        for (const auto &permission : m["permissions"]) {
            require(permission.is_string(), "invalid permission");
            auto value = permission.get<std::string>();
            require((value == "network" || value == "files.import" || value == "root.exec") &&
                        seen.insert(value).second,
                    "unknown or duplicate permission");
        }
    }
    bool root_exec = m.contains("permissions") &&
                     std::find(m["permissions"].begin(), m["permissions"].end(), "root.exec") !=
                         m["permissions"].end();
    require(root_exec == m.contains("root"), "root.exec requires an explicit root helper");
    if (root_exec) {
        require(!builtin && m["root"].is_object() && m["root"].size() == 2,
                "invalid root helper declaration");
        const auto &r = m["root"];
        require(r.contains("entry") && r["entry"].is_string() &&
                    safe_path(r["entry"].get<std::string>()) && r["entry"] != m.at("entry"),
                "root helper must be a separate package executable");
        require(r.contains("description") && r["description"].is_string(), "missing root purpose");
        auto description = r["description"].get<std::string>();
        require(!description.empty() && description.size() <= 240 &&
                    std::none_of(description.begin(), description.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "invalid root purpose");
    }
    for (const auto *key : {"name", "name_en"}) {
        if (std::string(key) == "name_en" && !m.contains(key)) continue;
        require(m.at(key).is_string(), "invalid application name");
        auto name = m.at(key).get<std::string>();
        require(!name.empty() && name.size() <= 160 &&
                    std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }),
                "application name must be 1..160 bytes without controls");
    }
    std::string entry = m.at("entry");
    require(builtin ? entry.starts_with("/usr/bin/") : safe_path(entry), "invalid executable path");
    if (m.contains("args")) {
        require(m["args"].is_array() && m["args"].size() <= 16, "invalid arguments");
        for (const auto &a : m["args"])
            require(a.is_string(), "invalid argument");
    }
    if (m.contains("handlers")) {
        static const std::regex mime_pattern("[a-z0-9][a-z0-9.+-]*/([a-z0-9][a-z0-9.+-]*|\\*)");
        static const std::regex scheme_pattern("[a-z][a-z0-9+.-]{0,31}");
        require(m["handlers"].is_array() && m["handlers"].size() <= 16, "invalid handlers");
        std::set<std::string> seen;
        for (const auto &h : m["handlers"]) {
            require(h.is_object() && h.size() == 2 && h.contains("action") &&
                        h["action"].is_string() && (h["action"] == "open" || h["action"] == "share"),
                    "invalid handler action");
            bool mime = h.contains("mime");
            require(mime ? h["mime"].is_string() : h.contains("scheme") && h["scheme"].is_string(),
                    "handler requires mime or scheme");
            std::string value = h[mime ? "mime" : "scheme"];
            require(value.size() <= 96 &&
                        std::regex_match(value, mime ? mime_pattern : scheme_pattern),
                    "invalid handler selector");
            require(mime || (h["action"] == "open" && value != "file" && value != "data" &&
                             value != "javascript" && value != "shell"), "forbidden URI scheme");
            require(seen.insert(h.dump()).second, "duplicate handler");
        }
    }
}
} // namespace leon
