#include "internal.hpp"
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>

namespace leon::detail {
Json installed_baseline(const fs::path &root, const std::string &id) {
    auto current = root / "apps" / id / "current";
    std::string instance;
    if (fs::exists(current / ".installation")) {
        require(fs::file_size(current / ".installation") == 32, "invalid installation identity");
        std::ifstream file(current / ".installation");
        file >> instance;
        require(instance.size() == 32, "cannot read installation identity");
    }
    return {{"manifest_sha256", file_digest(current / "manifest.json")}, {"instance", instance}};
}
} // namespace leon::detail

namespace leon {
using namespace detail;
Json Packages::inspect(const fs::path &filename) {
    fs::create_directories(root);
    Fd lock(open((root / "installer.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
    require(!flock(lock, LOCK_EX), "installer lock failed");
    auto reviews = root / "reviews";
    // Writers share the same lock. A previous crashed inspector cannot still
    // own any of these unpublished files.
    fs::remove_all(reviews);
    fs::create_directories(reviews);
    fs::permissions(reviews, fs::perms::owner_all);
    try {
        auto manifest = unpack_and_validate(filename, reviews);
        Json package;
        for (const char *key : {"id", "name", "name_en", "version", "arch", "lmui", "permissions"})
            if (manifest.contains(key)) package[key] = manifest.at(key);
        if (!package.contains("permissions")) package["permissions"] = Json::array();
        if (manifest.contains("root")) package["root_description"] = manifest["root"]["description"];
        std::uintmax_t bytes = 0;
        for (const auto &entry : fs::recursive_directory_iterator(reviews))
            if (entry.is_regular_file()) bytes += entry.file_size();
        Json result = {{"package", package}, {"baseline", nullptr}, {"installed", nullptr},
                       {"source", "local-unsigned"}, {"signature", "unsigned"},
                       {"sha256", file_digest(filename)}, {"unpacked_bytes", bytes},
                       {"installable", true}, {"reason", ""},
                       {"added_permissions", package["permissions"]}};
        auto id = manifest.at("id").get<std::string>();
        for (const auto &app : list()) {
            if (app.at("id") != id) continue;
            bool builtin_app = app.value("builtin", false);
            result["installed"] = {{"version", app.at("version")}, {"builtin", builtin_app}};
            if (builtin_app) {
                result["installable"] = false;
                result["reason"] = "cannot replace a system application";
            } else {
                result["baseline"] = installed_baseline(root, id);
                if (version(manifest.at("version")) <= version(app.at("version"))) {
                    result["installable"] = false;
                    result["reason"] = "version must increase";
                }
            }
            auto old = app.value("permissions", Json::array());
            result["added_permissions"] = Json::array();
            for (const auto &permission : package["permissions"])
                if (std::find(old.begin(), old.end(), permission) == old.end())
                    result["added_permissions"].push_back(permission);
        }
        fs::remove_all(reviews);
        return result;
    } catch (...) {
        fs::remove_all(reviews);
        throw;
    }
}
} // namespace leon
