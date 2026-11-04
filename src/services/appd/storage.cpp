#include "storage.hpp"
#include "internal.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace leon::appd {
namespace {
struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) {
        if (fd < 0)
            throw std::runtime_error(std::strerror(errno));
    }
    ~Fd() { close(value); }
};
struct CloseDirectory {
    void operator()(DIR *dir) const { closedir(dir); }
};
struct Walk {
    using Clock = std::chrono::steady_clock;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(1);
    unsigned entries{};
    uint64_t bytes{};
    dev_t device{};
    std::set<std::pair<dev_t, ino_t>> seen;
    void check(unsigned depth) {
        fail(depth <= 48 && ++entries <= 50000 && Clock::now() < deadline,
             "storage walk limit exceeded");
    }
    void visit(int parent, const char *name, unsigned depth, bool erase) {
        check(depth);
        struct stat st {};
        if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) {
            if (errno == ENOENT && !erase)
                return; // A running app may remove a file during measurement.
            throw std::runtime_error(std::strerror(errno));
        }
        fail(st.st_dev == device, "storage crosses a mount point");
        if (seen.emplace(st.st_dev, st.st_ino).second)
            bytes += uint64_t(st.st_blocks) * 512;
        if (S_ISDIR(st.st_mode)) {
            Fd fd(openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            struct stat opened {};
            fail(!fstat(fd.value, &opened) && opened.st_dev == st.st_dev &&
                     opened.st_ino == st.st_ino,
                 "storage changed during walk");
            std::unique_ptr<DIR, CloseDirectory> dir(fdopendir(dup(fd.value)));
            fail(bool(dir), "cannot read storage directory");
            for (;;) {
                errno = 0;
                auto *entry = readdir(dir.get());
                if (!entry) {
                    fail(!errno, "cannot read storage entries");
                    break;
                }
                if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
                    visit(fd.value, entry->d_name, depth + 1, erase);
            }
        }
        if (erase)
            fail(!unlinkat(parent, name, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0),
                 "cannot remove application data");
    }
};
bool exists_at(int parent, const std::string &name, struct stat &st) {
    if (!fstatat(parent, name.c_str(), &st, AT_SYMLINK_NOFOLLOW))
        return true;
    fail(errno == ENOENT, "cannot inspect application storage");
    return false;
}
} // namespace
Json storage_usage(const fs::path &path, bool missing_is_empty) {
    try {
        // Parents are broker/image-owned; the final component and all children
        // are inspected without following links, including a replaced home.
        int raw = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (raw < 0 && errno == ENOENT && missing_is_empty)
            return {{"available", true}, {"bytes", 0}};
        Fd parent(raw);
        struct stat st {};
        bool exists = exists_at(parent.value, path.filename().string(), st);
        fail(exists || missing_is_empty, "application executable missing");
        Walk walk;
        if (exists) {
            struct stat parent_stat {};
            fail(!fstat(parent.value, &parent_stat), "cannot inspect storage parent");
            walk.device = parent_stat.st_dev;
            walk.visit(parent.value, path.filename().c_str(), 0, false);
        }
        return {{"available", true}, {"bytes", walk.bytes}};
    } catch (const std::exception &e) {
        return {{"available", false}, {"error", e.what()}};
    }
}
void erase_private_tree(const fs::path &path, const std::string &name) {
    fail(!name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos,
         "invalid private storage name");
    int raw = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (raw < 0 && errno == ENOENT)
        return;
    Fd parent(raw);
    struct stat st {};
    if (!exists_at(parent.value, name, st))
        return;
    Walk walk;
    struct stat parent_stat {};
    fail(!fstat(parent.value, &parent_stat), "cannot inspect storage parent");
    walk.device = parent_stat.st_dev;
    walk.visit(parent.value, name.c_str(), 0, true);
    fail(!fsync(parent.value), "cannot persist application data removal");
}
} // namespace leon::appd
