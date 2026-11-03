#pragma once
#include <cstdint>
#include <string>

namespace leon::apps::settings {
struct FilesystemUsage {
    bool available{};
    std::uint64_t total{}, used{}, free{};
    bool operator==(const FilesystemUsage &) const = default;
};
struct DeviceInfo {
    bool connected{};
    std::string interface, address, hardware_address, kernel, architecture;
    FilesystemUsage system, data;
    bool operator==(const DeviceInfo &) const = default;
};
DeviceInfo read_device_info();
std::string format_bytes(std::uint64_t bytes);
} // namespace leon::apps::settings
