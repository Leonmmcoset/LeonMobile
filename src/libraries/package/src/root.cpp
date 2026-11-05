#include "internal.hpp"
#include <algorithm>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

namespace leon {
using namespace detail;
namespace {
fs::path trusted(const fs::path &path, bool file) {
    auto resolved = fs::canonical(path);
    for (auto part = resolved; !part.empty(); part = part.parent_path()) {
        struct stat s {};
        require(!lstat(part.c_str(), &s) && s.st_uid == 0 && !(s.st_mode & 022) &&
                    (part == resolved && file ? S_ISREG(s.st_mode) && s.st_nlink == 1
                                              : S_ISDIR(s.st_mode)),
                "root helper paths must be owned exclusively by the system");
        if (part == part.root_path())
            break;
    }
    return resolved;
}
} // namespace
Json Packages::root_identity(uid_t uid, pid_t pid) const {
    require(uid >= 12000 && uid < 60000 && pid > 1, "not a third-party application");
    Fd lock(open((root / "installer.lock").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat info {};
    require(!fstat(lock, &info) && S_ISREG(info.st_mode) && !info.st_uid && !(info.st_mode & 077) &&
                info.st_nlink == 1 && !flock(lock, LOCK_SH),
            "installer identity unavailable");
    Json m;
    for (const auto &app : list())
        if (!app.value("builtin", true) && app.at("uid").get<uid_t>() == uid) {
            require(m.is_null(), "ambiguous application identity");
            m = app;
        }
    require(!m.is_null() && m.contains("root"), "application does not declare root.exec");
    auto directory = trusted(root / "apps" / m.at("id").get<std::string>() / "current", false);
    auto entry = trusted(directory / m.at("entry").get<std::string>(), true);
    auto helper = trusted(directory / m.at("root").at("entry").get<std::string>(), true);
    require(entry != helper, "root helper is also the application entry");
    auto marker = trusted(directory / ".installation", true);
    Fd fd(open(marker.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    char token[33]{};
    require(
        read(fd, token, sizeof(token)) == 32 &&
            std::all_of(token, token + 32,
                        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
        "invalid installation identity");
    struct stat installed {
    }, caller{};
    require(!stat(entry.c_str(), &installed) &&
                !stat(("/proc/" + std::to_string(pid) + "/exe").c_str(), &caller) &&
                installed.st_dev == caller.st_dev && installed.st_ino == caller.st_ino,
            "request must originate in the installed application executable");
    m["installation"] = std::string(token, 32);
    m["executable"] = entry.string();
    m["helper"] = helper.string();
    return m;
}
} // namespace leon
