#include <iostream>
#include <leon/ipc.hpp>
#include <leon/package.hpp>
#include <unistd.h>
static leon::Json broker(const leon::Json &message) {
    auto response = leon::Json::parse(leon::request("/run/leon/apps.sock", message));
    if (!response.value("ok", false))
        throw std::runtime_error(response.value("error", "service error"));
    return response;
}
int main(int argc, char **argv) {
    try {
        leon::Packages packages;
        int i = 1;
        bool offline = false;
        if (i + 1 < argc && std::string(argv[i]) == "--root") {
            offline = true;
            packages.root = argv[i + 1];
            packages.builtin = packages.root / "builtin";
            i += 2;
        }
        if (i >= argc)
            throw std::runtime_error(
                "usage: lmpkg [--root DIR] list | install FILE | remove ID "
                "[--purge-data] | launch ID | save ID | close ID | running | lifecycle");
        std::string verb = argv[i++];
        if ((verb == "running" || verb == "lifecycle") && i == argc) {
            auto response = broker({{"op", verb == "running" ? "list" : "lifecycle"}});
            std::cout << (verb == "running" ? response.at("running") : response).dump() << '\n';
            return 0;
        }
        if (verb == "list" && i == argc) {
            std::cout << packages.list().dump(2) << '\n';
            return 0;
        }
        if (i >= argc)
            throw std::runtime_error("missing argument");
        std::string argument = argv[i++];
        if ((verb == "launch" || verb == "save" || verb == "close") && i == argc)
            std::cout << broker({{"op", verb}, {"id", argument}}).dump() << '\n';
        else {
            if (geteuid() && !offline) {
                if (verb == "install" && i == argc)
                    std::cout << broker({{"op", "install"},
                                         {"file", std::filesystem::absolute(argument).string()}})
                                     .dump()
                              << '\n';
                else if (verb == "remove" &&
                         (i == argc || (i + 1 == argc && std::string(argv[i]) == "--purge-data")))
                    std::cout
                        << broker({{"op", "remove"}, {"id", argument}, {"purge", i != argc}}).dump()
                        << '\n';
                else
                    throw std::runtime_error("unknown command or arguments");
                return 0;
            }
            if (verb == "install" && i == argc)
                packages.install(argument);
            else if (verb == "remove") {
                bool purge = i < argc && std::string(argv[i]) == "--purge-data";
                if (i + int(purge) != argc)
                    throw std::runtime_error("unknown argument");
                if (!offline)
                    try {
                        broker({{"op", "stop"}, {"id", argument}});
                    } catch (...) {
                    }
                packages.remove(argument, purge);
            } else
                throw std::runtime_error("unknown command or arguments");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "lmpkg: " << e.what() << '\n';
        return 1;
    }
}
