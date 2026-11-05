#include <iostream>
#include <leon/root.hpp>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <thread>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 3 || !getuid())
        return 2;
    setenv("LEON_ROOT_SOCKET", argv[1], 1);
    auto request = nlohmann::json::parse(argv[2]);
    std::stop_source stop;
    std::jthread input([&](std::stop_token done) {
        while (!done.stop_requested()) {
            pollfd p{0, POLLIN, 0};
            if (poll(&p, 1, 50) > 0) {
                char byte;
                if (read(0, &byte, 1) == 1)
                    stop.request_stop();
                return;
            }
        }
    });
    auto result = leon::request_root(
        request.at("args").get<std::vector<std::string>>(), stop.get_token(),
        [](leon::RootPhase phase) {
            std::cout << nlohmann::json{{"ok", true},
                                        {"state",
                                         phase == leon::RootPhase::pending ? "pending" : "running"}}
                             .dump()
                      << std::endl;
        });
    std::string hex;
    for (unsigned char byte : result.output) {
        hex += "0123456789abcdef"[byte >> 4];
        hex += "0123456789abcdef"[byte & 15];
    }
    std::cout << nlohmann::json{{"ok", true}, {"output_hex", hex}}.dump() << std::endl;
    std::cout << nlohmann::json{{"ok", result.ok()},
                                {"state", "finished"},
                                {"status", result.status},
                                {"cancelled", result.cancelled},
                                {"error", result.error}}
                     .dump()
              << std::endl;
    std::cout << nlohmann::json{{"client_uid", getuid()}}.dump() << std::endl;
}
