#include "device.hpp"
#include <arpa/inet.h>
#include <fstream>
#include <ifaddrs.h>
#include <iomanip>
#include <leon/system.hpp>
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
    try {
        auto network = system_request("network", {{"op", "status"}});
        info.interface = network.value("managed_interface", "");
        for (const auto &item : network.at("interfaces"))
            if (item.value("name", "") == info.interface) {
                info.address = item.value("ipv4", "");
                info.connected = item.value("up", false) && item.value("running", false);
            }
    } catch (...) {
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
