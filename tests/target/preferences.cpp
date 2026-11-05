// Test-only fixture injected into a disposable QEMU image. Never installed by CMake.
#include <arpa/inet.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern "C" {
#include "system_probe.h"
}
using J = nlohmann::json;
static J call(const char *service, J request, uid_t uid = 0) {
    auto text = request.dump();
    if (probe_rpc(service, text.c_str(), uid))
        throw std::runtime_error("fixture transport failed");
    return J::parse(probe_reply());
}
static J ok(const char *service, J request, uid_t uid = 0) {
    auto result = call(service, std::move(request), uid);
    if (!result.value("ok", false))
        throw std::runtime_error(result.dump());
    return result;
}
static void restart() {
    DIR *directory = opendir("/proc");
    if (!directory)
        throw std::runtime_error("cannot inspect guest processes");
    while (auto *entry = readdir(directory)) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;
        std::ifstream file(std::string("/proc/") + entry->d_name + "/comm");
        std::string name;
        std::getline(file, name);
        if (name == "notificationd" || name == "timed")
            kill(std::stoi(entry->d_name), SIGKILL);
    }
    closedir(directory);
    usleep(200000);
    for (int i = 0; i < 150; ++i) {
        try {
            ok("notifications", {{"op", "notification_policy"}}, 11000);
            ok("time", {{"op", "status"}});
            return;
        } catch (...) {
            usleep(100000);
        }
    }
    throw std::runtime_error("services did not recover");
}
static void stamp(unsigned char *p, const timespec &now) {
    auto seconds = htonl(uint32_t(int64_t(now.tv_sec) + 2208988800LL));
    auto fraction = htonl(uint32_t((uint64_t(now.tv_nsec) << 32) / 1000000000ULL));
    memcpy(p, &seconds, 4);
    memcpy(p + 4, &fraction, 4);
}
static J sync_time() {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{
        .sin_family = AF_INET, .sin_port = htons(123), .sin_addr = {htonl(INADDR_LOOPBACK)}};
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)))
        throw std::runtime_error("cannot bind fixture SNTP server");
    auto child = fork();
    if (!child) {
        alarm(8);
        sockaddr_in peer{};
        socklen_t size = sizeof(peer);
        unsigned char request[48], reply[48]{};
        if (recvfrom(fd, request, sizeof(request), 0, reinterpret_cast<sockaddr *>(&peer), &size) !=
            48)
            _exit(1);
        reply[0] = 0x24;
        reply[1] = 2;
        memcpy(reply + 24, request + 40, 8);
        timespec now{};
        clock_gettime(CLOCK_REALTIME, &now);
        stamp(reply + 32, now);
        stamp(reply + 40, now);
        _exit(sendto(fd, reply, sizeof(reply), 0, reinterpret_cast<sockaddr *>(&peer), size) == 48
                  ? 0
                  : 2);
    }
    close(fd);
    if (child < 0)
        throw std::runtime_error("cannot fork fixture SNTP server");
    try {
        ok("time", {{"op", "configure"}, {"automatic", true}, {"server", "127.0.0.1"}}, 11000);
        for (int i = 0; i < 70; ++i) {
            auto state = ok("time", {{"op", "status"}});
            if (state.value("synchronized", false)) {
                int status{};
                waitpid(child, &status, 0);
                if (!WIFEXITED(status) || WEXITSTATUS(status))
                    throw std::runtime_error("fixture SNTP response failed");
                return state;
            }
            usleep(100000);
        }
        throw std::runtime_error("real SNTP synchronization timed out");
    } catch (...) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        throw;
    }
}
int main(int argc, char **argv) {
    // Fixed operations only; setuid exists solely on the private injected image.
    if (argc != 2 || geteuid() || (getuid() != 0 && getuid() != 1000))
        return 2;
    try {
        J out;
        std::string op = argv[1];
        if (op == "snapshot") {
            out = {{"time", ok("time", {{"op", "status"}})},
                   {"policy", ok("notifications", {{"op", "notification_policy"}}, 11000)},
                   {"queue", ok("notifications", {{"op", "notifications"}})},
                   {"desktop", ok("notifications", {{"op", "notifications"}}, 1000)},
                   {"settings", ok("settings", {{"op", "status"}})},
                   {"auth", ok("auth", {{"op", "status"}})}};
        } else if (op == "post") {
            out = ok("notifications",
                     {{"op", "notify"}, {"title", "Private LeonMobile notification"}}, 11002);
        } else if (op == "restart") {
            restart();
            out = {{"ok", true}};
        } else if (op == "sync") {
            out = sync_time();
        } else if (op == "authorization") {
            for (auto service : {"time", "notifications"}) {
                auto request = std::string(service) == "time"
                                   ? J{{"op", "configure"}, {"automatic", false}}
                                   : J{{"op", "configure"}, {"values", {{"dnd", true}}}};
                if (call(service, request, 11002).value("ok", true))
                    throw std::runtime_error("untrusted policy mutation");
            }
            if (call("notifications", {{"op", "notifications"}}, 11002).value("ok", true))
                throw std::runtime_error("untrusted queue read");
            if (call("audio", {{"op", "notification_sound"}}, 11002).value("ok", true))
                throw std::runtime_error("untrusted system sound");
            out = {{"ok", true}};
        } else
            return 2;
        puts(out.dump().c_str());
        return 0;
    } catch (const std::exception &error) {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
