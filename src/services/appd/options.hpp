#pragma once
#include <filesystem>
#include <string>
namespace leon::appd {
struct Options {
    std::filesystem::path root{"/data/leon"}, builtin{"/usr/share/leon/apps"};
    std::string socket_name{"/run/leon/apps.sock"}, runtime_prefix{"/run/user"};
    std::string notifications_socket{"/run/leon/notifications.sock"};
    std::string root_socket{"/run/leon-root/control.sock"};
    std::string portal_socket{"/run/leon/portal.sock"};
    std::string settings_socket;
    std::string wayland_socket{"/run/leon/wayland/wayland-0"};
    std::string log_socket;
    bool development{};
};
} // namespace leon::appd
