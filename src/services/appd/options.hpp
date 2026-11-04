#pragma once
#include <filesystem>
#include <string>
namespace leon::appd {
struct Options {
    std::filesystem::path root{"/data/leon"}, builtin{"/usr/share/leon/apps"};
    std::string socket_name{"/run/leon/apps.sock"}, runtime_prefix{"/run/user"};
    std::string notifications_socket{"/run/leon/notifications.sock"};
    std::string wayland_socket{"/run/leon/wayland/wayland-0"};
    bool development{};
};
} // namespace leon::appd
