#include "device.hpp"
#include <arpa/inet.h>
#include <fstream>
#include <ifaddrs.h>
#include <iomanip>
#include <net/if.h>
#include <sstream>
#include <sys/statvfs.h>
#include <sys/utsname.h>

namespace leon::apps::settings {
static FilesystemUsage usage(const char *path) {
    struct statvfs status {};
    if (statvfs(path, &status))
        return {};
    return {true, status.f_blocks * status.f_frsize,
            (status.f_blocks - status.f_bfree) * status.f_frsize,
            status.f_bavail * status.f_frsize};
}
DeviceInfo read_device_info() {
    DeviceInfo info;
    struct utsname system {};
    if (!uname(&system)) {
        info.kernel = system.release;
        info.architecture = system.machine;
    }
    ifaddrs *interfaces = nullptr;
    if (!getifaddrs(&interfaces)) {
        for (auto *item = interfaces; item; item = item->ifa_next) {
            if (!item->ifa_name || !item->ifa_addr || (item->ifa_flags & IFF_LOOPBACK))
                continue;
            if (info.interface.empty())
                info.interface = item->ifa_name;
            if (item->ifa_addr->sa_family != AF_INET)
                continue;
            bool connected = (item->ifa_flags & IFF_UP) && (item->ifa_flags & IFF_RUNNING);
            if (info.address.empty() || (!info.connected && connected)) {
                char address[INET_ADDRSTRLEN]{};
                auto *ipv4 = reinterpret_cast<sockaddr_in *>(item->ifa_addr);
                if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) {
                    info.interface = item->ifa_name;
                    info.address = address;
                    info.connected = connected;
                }
            }
        }
        freeifaddrs(interfaces);
    }
    if (!info.interface.empty()) {
        std::ifstream mac("/sys/class/net/" + info.interface + "/address");
        mac >> info.hardware_address;
    }
    info.system = usage("/");
    info.data = usage("/data");
    return info;
}
std::string format_bytes(std::uint64_t bytes) {
    static constexpr const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    unsigned unit = 0;
    while (value >= 1024 && unit < 4) {
        value /= 1024;
        ++unit;
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(unit >= 3 ? 1 : 0) << value << " " << units[unit];
    return text.str();
}
} // namespace leon::apps::settings
