#include "internal.hpp"
#include <fcntl.h>
#include <sys/file.h>
namespace leon {
using namespace detail;
void Packages::install(const fs::path &filename) {
    fs::create_directories(root);
    Fd lock(
        open((root / "installer.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
    require(!flock(lock, LOCK_EX), "installer lock failed");
    fs::create_directories(root / "staging");
    /* Only the locked installer owns staging; interrupted transactions are safe to collect. */
    for (const auto &e : fs::directory_iterator(root / "staging"))
        fs::remove_all(e.path());
    fs::path stage = root / "staging" / std::to_string(getpid());
    fs::create_directory(stage);
    try {
        auto m = unpack_and_validate(filename, stage);
        std::string id = m.at("id"), ver = m.at("version");
        for (const auto &app : list())
            if (app.at("id") == id) {
                require(!app.value("builtin", false), "cannot replace a system application");
                require(version(ver) > version(app.at("version")), "version must increase");
            }
        Json identities = fs::exists(root / "identities.json")
                              ? read_json(root / "identities.json")
                              : Json{{"next", 12000}, {"apps", Json::object()}};
        if (!identities["apps"].contains(id)) {
            unsigned uid = identities.at("next");
            require(uid < 60000, "application identities exhausted");
            identities["apps"][id] = uid;
            identities["next"] = uid + 1;
            atomic_json(root / "identities.json", identities);
        }
        auto appdir = root / "apps" / id;
        fs::create_directories(appdir / "versions");
        auto dest = appdir / "versions" / ver;
        // An interrupted transaction may have published this unused version
        // directory before switching current. Validation above guarantees that
        // this version is newer than the active one, so it is safe to replace.
        if (fs::exists(dest))
            fs::remove_all(dest);
        durable_dir(stage);
        fs::rename(stage, dest);
        durable_dir(appdir / "versions");
        auto next = appdir / "next";
        fs::remove(next);
        fs::create_symlink(fs::path("versions") / ver, next);
        fs::rename(next, appdir / "current");
        durable_dir(appdir);
    } catch (...) {
        fs::remove_all(stage);
        throw;
    }
}
void Packages::remove(const std::string &id, bool purge) {
    require(valid_id(id), "invalid application ID");
    fs::create_directories(root);
    Fd lock(
        open((root / "installer.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
    require(!flock(lock, LOCK_EX), "installer lock failed");
    auto m = lookup(id);
    require(!m.value("builtin", false), "cannot remove a system application");
    auto appdir = root / "apps" / id;
    fs::remove(appdir / "current");
    durable_dir(appdir);
    fs::remove_all(appdir);
    if (purge)
        fs::remove_all(root / "data" / id);
}
} // namespace leon
