#include "control.hpp"
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace leon::shell {
bool control(const std::string &command) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, "/run/leon/wm.sock");
    bool ok = !connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
    if (ok)
        send(fd, command.data(), command.size(), MSG_NOSIGNAL);
    close(fd);
    return ok;
}
} // namespace leon::shell
