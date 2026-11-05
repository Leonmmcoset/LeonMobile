#include "wire.h"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <leon/root.hpp>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace leon {
namespace {
struct Connection {
    int fd{-1};
    ~Connection() {
        if (fd >= 0)
            close(fd);
    }
};
int connect_application() {
    const char *path = std::getenv("LEON_ROOT_SOCKET");
    if (!path)
        path = LEON_ROOT_SOCKET;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(address.sun_path))
        return -1;
    strcpy(address.sun_path, path);
    Connection connection;
    connection.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct ucred peer {};
    socklen_t size = sizeof(peer);
    if (connection.fd < 0 ||
        connect(connection.fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ||
        getsockopt(connection.fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid ||
        fcntl(connection.fd, F_SETFL, 0))
        return -1;
    struct timeval receive {
        125, 0
    }, send{3, 0};
    if (setsockopt(connection.fd, SOL_SOCKET, SO_RCVTIMEO, &receive, sizeof(receive)) ||
        setsockopt(connection.fd, SOL_SOCKET, SO_SNDTIMEO, &send, sizeof(send)))
        return -1;
    int result = connection.fd;
    connection.fd = -1;
    return result;
}
} // namespace
RootResult request_root(const std::vector<std::string> &arguments, std::stop_token stop,
                        std::function<void(RootPhase)> phase) {
    RootResult result;
    if (stop.stop_requested()) {
        result.cancelled = true;
        return result;
    }
    Connection connection{connect_application()};
    if (connection.fd < 0) {
        result.error = "Root service unavailable";
        return result;
    }
    // Destroy this callback before closing/reusing its descriptor.
    std::stop_callback cancel(stop, [&] { shutdown(connection.fd, SHUT_RDWR); });
    try {
        auto request = nlohmann::json{{"op", "app_request"}, {"args", arguments}}.dump() + "\n";
        if (request.size() >= LEON_ROOT_MESSAGE || root_send(connection.fd, request.c_str(), -1))
            throw std::runtime_error("Cannot send Root request");
        int state = 0;
        char response[LEON_ROOT_MESSAGE];
        for (;;) {
            if (root_receive(connection.fd, response, sizeof(response), nullptr))
                throw std::runtime_error("Root request disconnected or service restarted");
            auto reply = nlohmann::json::parse(response);
            if (!reply.value("ok", false)) {
                result.error = reply.value("error", "Root request rejected");
                break;
            }
            auto next = reply.value("state", "");
            if (next == "pending" && state == 0) {
                state = 1;
                if (phase)
                    phase(RootPhase::pending);
            } else if (next == "running" && state == 1) {
                state = 2;
                if (phase)
                    phase(RootPhase::running);
            } else if (next == "finished" && state == 2) {
                result.status = reply.at("status").get<int>();
                break;
            } else if (reply.contains("output_hex") && state == 2) {
                auto hex = reply.at("output_hex").get<std::string>();
                if (hex.size() % 2 || result.output.size() + hex.size() / 2 > 65536)
                    throw std::runtime_error("Invalid Root output length");
                auto digit = [](char c) {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    throw std::runtime_error("Invalid Root output encoding");
                };
                for (size_t i = 0; i < hex.size(); i += 2)
                    result.output.push_back(
                        static_cast<char>(digit(hex[i]) * 16 + digit(hex[i + 1])));
            } else
                throw std::runtime_error("Invalid Root response sequence");
        }
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    result.cancelled = stop.stop_requested();
    if (result.cancelled)
        result.error.clear();
    return result;
}
} // namespace leon
