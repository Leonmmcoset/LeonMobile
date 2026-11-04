#include "broker.hpp"
#include <iostream>
#include <unistd.h>
int main(int argc, char **argv) {
    if (geteuid()) {
        std::cerr << "leon-launchd requires root\n";
        return 1;
    }
    leon::appd::Options options;
    for (int i = 1; i < argc; ++i) {
        std::string option = argv[i];
        if (i + 1 == argc) {
            std::cerr << "Missing option value\n";
            return 2;
        }
        std::string value = argv[++i];
        if (option == "--root") {
            options.root = value;
            options.development = true;
        } else if (option == "--builtin")
            options.builtin = value;
        else if (option == "--socket")
            options.socket_name = value;
        else if (option == "--notifications-socket")
            options.notifications_socket = value;
        else if (option == "--runtime-dir")
            options.runtime_prefix = value;
        else if (option == "--wayland")
            options.wayland_socket = value;
        else {
            std::cerr << "Unknown option: " << option << '\n';
            return 2;
        }
    }
    try {
        leon::appd::Broker broker(options);
        return leon::appd::serve(broker, options.socket_name);
    } catch (const std::exception &error) {
        std::cerr << "leon-launchd: " << error.what() << '\n';
        return 1;
    }
}
