#include "application.hpp"
#include "widgets.hpp"

using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
void SettingsApplication::paint_device(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    if (page_ == Page::network) {
        c.rounded({20, 164, w - 40, 100}, t.container, 26);
        settings::icon(c, settings::Icon::network, 54, 214, t.primary);
        c.text(tr(en, "以太网", "Ethernet"), {88, 178, w - 120, 32}, 20, t.text, true);
        c.text(device_.connected ? tr(en, "已连接", "Connected")
                                 : tr(en, "未连接", "Not connected"),
               {88, 216, w - 120, 28}, 14, t.muted);
        auto unavailable = tr(en, "不可用", "Unavailable");
        settings::information(c, 296, tr(en, "网络接口", "Network interface"),
                              device_.interface.empty() ? unavailable : device_.interface);
        settings::information(c, 380, tr(en, "IP 地址", "IP address"),
                              device_.address.empty() ? unavailable : device_.address);
        settings::information(c, 464, tr(en, "硬件地址", "Hardware address"),
                              device_.hardware_address.empty() ? unavailable
                                                               : device_.hardware_address);
    } else if (page_ == Page::storage) {
        double y = 164;
        for (bool data : {true, false}) {
            const auto &usage = data ? device_.data : device_.system;
            c.rounded({20, y, w - 40, 204}, t.surface, 26);
            c.text(data ? tr(en, "数据存储", "Data storage") : tr(en, "系统存储", "System storage"),
                   {40, y + 16, w - 80, 32}, 18, t.text, true);
            if (usage.available) {
                c.text(settings::format_bytes(usage.used) + " / " +
                           settings::format_bytes(usage.total),
                       {40, y + 58, w - 80, 40}, 24, t.text);
                c.rounded({40, y + 118, w - 80, 10}, t.container, 5);
                double fraction = usage.total ? double(usage.used) / usage.total : 0;
                if (fraction > 0)
                    c.rounded({40, y + 118, (w - 80) * fraction, 10}, t.primary, 5);
                c.text(tr(en, "可用 ", "Available: ") + settings::format_bytes(usage.free),
                       {40, y + 150, w - 80, 28}, 14, t.muted);
            } else
                c.text(tr(en, "存储信息不可用", "Storage information unavailable"),
                       {40, y + 66, w - 80, 60}, 16, t.muted);
            y += 220;
        }
    } else {
        c.rounded({20, 164, w - 40, 112}, t.container, 28);
        c.text("LeonMobile", {40, 176, w - 80, 48}, 30, t.primary, true);
        c.text(tr(en, "版本 1.0.0", "Version 1.0.0"), {40, 230, w - 80, 28}, 16, t.text);
        settings::information(c, 304, tr(en, "设备", "Device"), "QEMU ARM64");
        settings::information(c, 388, tr(en, "处理器架构", "Architecture"), device_.architecture);
        settings::information(c, 472, tr(en, "内核版本", "Kernel version"), device_.kernel);
        settings::information(c, 556, tr(en, "构建类型", "Build type"),
                              tr(en, "开发者图形原型", "Developer graphics prototype"));
    }
}
} // namespace leon::apps
