#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
namespace leon {
namespace fs = std::filesystem;
using Json = nlohmann::json;
bool valid_id(const std::string &id);
Json read_json(const fs::path &path);
void atomic_json(const fs::path &path, const Json &data);
void validate_manifest(const Json &m, bool builtin = false);
class Packages {
public:
    explicit Packages(fs::path root = "/data/leon", fs::path builtin = "/usr/share/leon/apps");
    Json list() const;
    Json lookup(const std::string &id) const;
    void install(const fs::path &archive);
    void remove(const std::string &id, bool purge);
    fs::path executable(const Json &manifest) const;
    fs::path root;
    fs::path builtin;
};
std::string request(const std::string &socket, const Json &message);
}
