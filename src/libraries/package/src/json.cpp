#include "internal.hpp"
#include <fcntl.h>
#include <fstream>
namespace leon::detail {
void durable_dir(const fs::path &path) {
    Fd fd(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    require(!fsync(fd), "directory sync failed");
}
void write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0 && errno == EINTR)
            continue;
        require(k > 0, "write failed");
        p += k;
        n -= size_t(k);
    }
}
} // namespace leon::detail
namespace leon {
using namespace detail;
Json read_json(const fs::path &path) {
    require(fs::file_size(path) <= 1024 * 1024, "JSON document too large");
    std::ifstream file(path);
    require(bool(file), "cannot read " + path.string());
    return Json::parse(file);
}
void atomic_json(const fs::path &path, const Json &data) {
    fs::create_directories(path.parent_path());
    auto temporary = path.string() + ".new";
    Fd fd(open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0644));
    std::string text = data.dump(2) + "\n";
    write_all(fd, text.data(), text.size());
    require(!fsync(fd), "JSON sync failed");
    fs::rename(temporary, path);
    durable_dir(path.parent_path());
}
} // namespace leon
