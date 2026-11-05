#include "catalog.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace leon::apps::settings {
const std::array<Entry, 16> &entries() {
    static const std::array<Entry, 16> catalog{{
        {Page::network, Icon::network, "网络与互联网", "Network & internet",
         "网络 互联网 以太网 IP ethernet connection", true},
        {Page::display, Icon::display, "显示", "Display",
         "显示 主题 深色 浅色 动画 动态 dark light theme animation motion", true},
        {Page::sound, Icon::sound, "声音", "Sound", "声音 音量 播放 volume audio playback", true},
        {Page::storage, Icon::storage, "存储", "Storage", "存储 空间 数据 磁盘 space disk data",
         true},
        {Page::system, Icon::system, "系统", "System", "系统 语言 导航 language navigation", true},
        {Page::about, Icon::about, "关于设备", "About device", "关于 设备 版本 内核 version kernel",
         true},
        {Page::applications, Icon::applications, "应用管理", "Apps",
         "应用 管理 权限 停止 清除 卸载 软件 applications permissions uninstall data", true},
        {Page::language, Icon::language, "语言", "Languages", "语言 中文 英文 简体 English Chinese",
         false},
        {Page::navigation, Icon::navigation, "系统导航", "System navigation",
         "导航 手势 三键 按钮 gestures buttons", false},
        {Page::developer, Icon::developer, "开发者选项", "Developer options",
         "开发者 root 内核 模块 核心 服务 developer kernel modules services", false},
        {Page::security, Icon::system, "安全与屏幕锁定", "Security & screen lock",
         "安全 锁屏 密码 PIN security password lock credential", false},
        {Page::diagnostics, Icon::developer, "系统诊断", "System diagnostics",
         "诊断 日志 崩溃 状态 logging logs crash diagnostics services", false},
        {Page::defaults, Icon::applications, "默认应用", "Default apps", "默认 打开 链接 分享 default open links share", false},
        {Page::documents, Icon::applications, "文件访问", "Document access", "文件 授权 撤销 documents grants revoke", false},
        {Page::datetime, Icon::system, "日期与时间", "Date & time", "日期 时间 时区 自动 校时 手动 24 date time clock timezone ntp", false},
        {Page::notifications, Icon::sound, "通知管理", "Notifications", "通知 勿扰 提示音 锁屏 隐私 notification sound silent disturb privacy", true},
    }};
    return catalog;
}
const Entry &entry(Page page) {
    for (const auto &item : entries())
        if (item.page == page)
            return item;
    throw std::logic_error("unknown settings page");
}
bool matches(const Entry &item, std::string_view query) {
    auto fold = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    auto haystack = std::string(item.chinese) + " " + std::string(item.english) + " " +
                    std::string(item.keywords);
    return fold(haystack).find(fold(std::string(query))) != std::string::npos;
}
} // namespace leon::apps::settings
