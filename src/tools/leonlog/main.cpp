#include "log.h"
#include <csignal>
#include <iomanip>
#include <iostream>
#include <leon/ipc.hpp>
#include <memory>
#include <poll.h>
#include <string>
#include <vector>
namespace {
volatile std::sig_atomic_t stopped;
void stop(int) { stopped = 1; }
constexpr const char *levels[] = {"emerg", "alert", "crit", "error", "warn", "notice", "info", "debug"};
int level(const std::string &text) {
    for (int i = 0; i < 8; ++i)
        if (text == levels[i] || text == std::to_string(i))
            return i;
    throw std::runtime_error("unknown log level");
}
long long number(const std::string &text) {
    size_t used;
    auto n = std::stoll(text, &used);
    if (used != text.size() || n < 0)
        throw std::runtime_error("invalid nonnegative integer");
    return n;
}
void usage() {
    std::cerr << "usage: leonlog [query|status|crashes|write TEXT] [--json] [--follow] [--previous]\n"
                 "  [--boot ID] [--after N] [--limit 1..128] [--level 0..7|info|warn|error|debug]\n"
                 "  [--source NAME] [--uid UID] [--pid PID] [--socket PATH] [--ingest PATH]\n";
}
/* Defensive display even if a damaged private journal supplied control bytes. */
std::string display(const std::string &text) {
    std::string out;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text)
        if (c < 32 || c == 127) {
            out += "\\x";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += static_cast<char>(c);
    return out;
}
}
int main(int argc, char **argv) {
    try {
        std::string socket = LEON_LOG_SOCKET, ingest = LEON_LOG_INGEST, op = "query", message;
        leon::Json request{{"op", "query"}, {"limit", 50}, {"tail", true}};
        bool json = false, follow = false;
        int severity = 6;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") { usage(); return 0; }
            if (arg == "--json") { json = true; continue; }
            if (arg == "--follow" || arg == "-f") { follow = true; continue; }
            if (arg == "--previous") { request["boot"] = "previous"; continue; }
            if (arg == "query" || arg == "status" || arg == "crashes") { op = arg; continue; }
            if (arg == "write") {
                op = arg;
                if (++i == argc)
                    throw std::runtime_error("write requires a message");
                message = argv[i];
                continue;
            }
            if (++i == argc)
                throw std::runtime_error("missing option value");
            std::string value = argv[i];
            if (arg == "--socket") socket = value;
            else if (arg == "--ingest") ingest = value;
            else if (arg == "--level") request["max_level"] = severity = level(value);
            else if (arg == "--source") request["source"] = value;
            else if (arg == "--boot") request["boot"] = value;
            else if (arg == "--uid") request["uid"] = number(value);
            else if (arg == "--pid") request["pid"] = number(value);
            else if (arg == "--limit") request["limit"] = number(value);
            else if (arg == "--after") { request["after"] = number(value); request["tail"] = false; }
            else throw std::runtime_error("unknown option: " + arg);
        }
        if (op == "write") {
            auto tag = request.value("source", std::string("leonlog"));
            if (leon_log_write(ingest.c_str(), severity, tag.c_str(), message.data(), message.size()))
                throw std::runtime_error("log message was not queued (unavailable, busy or too long)");
            return 0;
        }
        if (follow && (op != "query" || request.value("boot", "current") != "current"))
            throw std::runtime_error("follow requires the current boot query");
        request["op"] = op;
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        auto out = std::make_unique<char[]>(LEON_LOG_RESPONSE);
        std::string boot;
        bool unavailable = false;
        while (!stopped) {
            auto text = request.dump() + "\n";
            if (leon_log_exchange(socket.c_str(), text.c_str(), out.get(), LEON_LOG_RESPONSE, 3000)) {
                if (!follow)
                    throw std::runtime_error("log service unavailable or timed out");
                if (!unavailable)
                    std::cerr << "leonlog: waiting for log service\n";
                unavailable = true;
                poll(nullptr, 0, 250);
                continue;
            }
            auto response = leon::Json::parse(out.get());
            if (!response.value("ok", false))
                throw std::runtime_error(response.value("error", "log query failed"));
            unavailable = false;
            if (follow && !boot.empty() && boot != response.value("boot", "")) {
                std::cerr << "leonlog: boot changed; resetting cursor\n";
                request.erase("after");
                request["tail"] = true;
                boot.clear();
                continue;
            }
            boot = response.value("boot", "");
            if (response.value("gap", false))
                std::cerr << "leonlog: cursor predates retained history; records may be missing\n";
            if (json || op != "query")
                std::cout << response.dump(json && !follow ? 2 : -1) << '\n';
            else
                for (const auto &r : response.at("records")) {
                    int l = r.value("level", 6);
                    std::cout << r.value("realtime_ms", 0LL) << ' ' << levels[l >= 0 && l < 8 ? l : 6]
                              << ' ' << r.value("uid", -1) << ':' << r.value("pid", -1)
                              << ' ' << display(r.value("scope", "")) << '/'
                              << display(r.value("source", "")) << ": "
                              << display(r.value("message", "")) << '\n';
                }
            std::cout.flush();
            if (!follow)
                break;
            request["after"] = response.at("next");
            request["tail"] = false;
            if (!response.value("more", false))
                poll(nullptr, 0, 250);
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "leonlog: " << e.what() << '\n';
        return 1;
    }
}
