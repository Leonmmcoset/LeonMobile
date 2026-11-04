#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <leon/ipc.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

int main() {
    // Do not rely on a caller ignoring SIGPIPE. A restarted daemon must become
    // a caught IPC error even when it disappears partway through a send.
    std::signal(SIGPIPE, SIG_DFL);
    char temporary[] = "/tmp/leon-ipc-XXXXXX";
    assert(mkdtemp(temporary));
    std::string endpoint = std::string(temporary) + "/peer.sock";
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(server >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, endpoint.c_str());
    assert(!bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)));
    assert(!listen(server, 4));
    std::thread peer([&] {
        for (int i = 0; i < 4; ++i) {
            int fd = accept(server, nullptr, nullptr);
            assert(fd >= 0);
            close(fd);
        }
    });
    // Exceed a Unix socket's send buffer: even if the first send wins the
    // scheduling race, another send must encounter the disappeared peer.
    leon::Json message = {{"op", "app_info"}, {"payload", std::string(2 * 1024 * 1024, 'x')}};
    for (int i = 0; i < 4; ++i) {
        bool failed = false;
        try {
            (void)leon::request(endpoint, message);
        } catch (const std::exception &) {
            failed = true;
        }
        assert(failed);
    }
    peer.join();
    close(server);
    unlink(endpoint.c_str());
    rmdir(temporary);
}
