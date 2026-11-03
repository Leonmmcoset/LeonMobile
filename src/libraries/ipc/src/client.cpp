#include <array>
#include <cerrno>
#include <cstring>
#include <leon/ipc.hpp>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
namespace leon {
namespace {
void require(bool ok, const std::string &message) {
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
static void write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0 && errno == EINTR)
            continue;
        require(k > 0, "write failed");
        p += k;
        n -= size_t(k);
    }
}
} // namespace
std::string request(const std::string &socket_path, const Json &message) {
    Fd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    timeval timeout{message.value("op", "") == "install" ? 60 : 3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    require(socket_path.size() < sizeof(address.sun_path), "socket path too long");
    strcpy(address.sun_path, socket_path.c_str());
    require(!connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)),
            "service unavailable: " + socket_path);
    auto text = message.dump() + "\n";
    write_all(fd, text.data(), text.size());
    shutdown(fd, SHUT_WR);
    std::string result;
    std::array<char, 4096> buf;
    ssize_t n;
    while ((n = read(fd, buf.data(), buf.size())) > 0) {
        result.append(buf.data(), size_t(n));
        require(result.size() < 1024 * 1024, "response too large");
    }
    require(n == 0, "service response timed out");
    return result;
}
} // namespace leon
