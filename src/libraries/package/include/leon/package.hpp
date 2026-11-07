#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/types.h>
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
    // Full validation without publication. Reviewed installs compare the current
    // installation again under the writer lock before changing any active app.
    Json inspect(const fs::path &archive);
    void install_reviewed(const fs::path &archive, const Json &review);
    void remove(const std::string &id, bool purge);
    fs::path executable(const Json &manifest) const;
    // Privileged broker adapter: authenticates the actual installed application ELF.
    Json root_identity(uid_t uid, pid_t pid) const;
    fs::path root;
    fs::path builtin;

  private:
    void install_impl(const fs::path &archive, const Json *review);
};
} // namespace leon
