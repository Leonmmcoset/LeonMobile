#include "broker.hpp"
#include "internal.hpp"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
namespace leon::appd {
static volatile sig_atomic_t quitting;
static void on_signal(int) { quitting = 1; }
int serve(Broker &broker, const std::string &socket_name) {
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    const char *socket_path = socket_name.c_str();
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_name.size() >= sizeof(address.sun_path))
        return 2;
    strcpy(address.sun_path, socket_path);
    unlink(socket_path);
    mode_t old = umask(0);
    if (bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ||
        listen(server, 16)) {
        perror(socket_path);
        return 1;
    }
    chmod(socket_path, 0666);
    umask(old);
    while (!quitting) {
        broker.reap();
        pollfd ready{server, POLLIN, 0};
        if (poll(&ready, 1, 200) <= 0)
            continue;
        int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        timeval timeout{1, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        Json response;
        try {
            ucred peer{};
            socklen_t len = sizeof(peer);
            fail(!getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &len),
                 "cannot identify client");
            std::string text;
            char ch;
            while (text.size() < 8192 && read(client, &ch, 1) == 1 && ch != '\n')
                text += ch;
            fail(text.size() < 8192, "request too large");
            response = broker.dispatch(Json::parse(text), peer.uid);
        } catch (const std::exception &e) {
            response = {{"ok", false}, {"error", e.what()}};
        }
        auto out = response.dump() + "\n";
        send(client, out.data(), out.size(), MSG_NOSIGNAL);
        close(client);
    }
    unlink(socket_path);
    close(server);
    return 0;
}
} // namespace leon::appd
