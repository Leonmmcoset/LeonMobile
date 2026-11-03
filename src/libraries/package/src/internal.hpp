#pragma once
#include <array>
#include <cerrno>
#include <cstring>
#include <leon/package.hpp>
#include <stdexcept>
#include <unistd.h>
namespace leon::detail {
inline void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Fd {
    int value;
    explicit Fd(int v) : value(v) {
        if (v < 0)
            throw std::runtime_error(std::strerror(errno));
    }
    ~Fd() { close(value); }
    operator int() const { return value; }
};
void write_all(int fd, const char *data, size_t size);
void durable_dir(const fs::path &path);
bool safe_path(const std::string &path);
std::array<unsigned, 3> version(const std::string &text);
Json unpack_and_validate(const fs::path &archive, const fs::path &stage);
} // namespace leon::detail
