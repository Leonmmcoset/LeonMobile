#include "control.hpp"
#include "shell.hpp"
#include <cstdlib>
#include <ctime>
using namespace mobile;
namespace leon::shell {
void Shell::system_refresh() {
    try {
        auto zone = system_request("time", {{"op", "status"}}).value("timezone", "");
        if (!zone.empty() && zone != timezone) {
            timezone = zone;
            setenv("TZ", zone.c_str(), 1);
            tzset();
        }
    } catch (...) {
    }

    try {
        auto power = system_request("power", {{"op", "status"}});
        power_state = power;
        if (power.value("lock_requested", false) && page != Page::lock)
            show_page(Page::lock);
        int locked = page == Page::lock;
        if (locked != reported_locked) {
            control(locked ? "lock" : "unlock");
            system_request("power", {{"op", "locked"}, {"value", bool(locked)}});
            reported_locked = locked;
        }
    } catch (...) {
    }
    try {
        auto network = system_request("network", {{"op", "status"}});
        connected = false;
        for (const auto &item : network.at("interfaces"))
            if (item.value("name", "") == network.value("managed_interface", "") &&
                item.value("up", false) && item.value("running", false))
                connected = true;
    } catch (...) {
        connected = false;
    }
    try {
        auto list = system_request("portal", {{"op", "pending"}}).at("requests");
        auto next = list.empty() ? Json::object() : list.front();
        if (next != permission && next.value("file_request", false))
            picker_open("");
        permission = std::move(next);
    } catch (...) {
        permission = Json::object();
    }
    bool show = !permission.empty() && page != Page::lock;
    if (show && !permission_focused) {
        control("modal");
        permission_focused = true;
    }
    if (!show && permission_focused) {
        control("unmodal");
        permission_focused = false;
        if (page == Page::active && !shade.visible())
            control("focus " + active_id);
    }
}
std::string Shell::power_label(bool compact) const {
    const auto &batteries = power_state.value("batteries", Json::array());
    if (!batteries.empty() && batteries.front().contains("percent"))
        return std::to_string(batteries.front().at("percent").get<int>()) + "%";
    if (power_state.value("external_power", -1) == 1)
        return compact ? "AC" : tr(en, "外部供电", "External power");
    return compact ? "—" : tr(en, "电源信息不可用", "Power status unavailable");
}
void Shell::permission_decide(bool allow) {
    system_request("portal",
                   {{"op", "decide"}, {"token", permission.at("token")}, {"allow", allow}});
    if (allow && permission.value("permission", "") == "network") {
        auto id = permission.at("app").get<std::string>();
        // Socket restrictions are inherited across exec. Restart with the new grant.
        if (running.contains(id)) {
            rpc({{"op", "stop"}, {"id", id}});
            launch(id);
        }
    }
    permission = Json::object();
    system_refresh();
    app.redraw();
}
void Shell::picker_open(const std::string &directory) {
    picker_files =
        system_request("portal", {{"op", "files"}, {"directory", directory}}).at("files");
    std::sort(picker_files.begin(), picker_files.end(), [](const Json &a, const Json &b) {
        if (a.at("directory") != b.at("directory"))
            return a.at("directory").get<bool>();
        return a.at("name").get<std::string>() < b.at("name").get<std::string>();
    });
    picker_directory = directory;
    picker_page = 0;
    app.redraw();
}
void Shell::picker_choose(const std::string &file) {
    system_request("portal", {{"op", "pick"}, {"token", permission.at("token")}, {"file", file}});
    system_refresh();
    app.redraw();
}
void Shell::paint_permission(Canvas &c) {
    if (permission.empty() || page == Page::lock)
        return;
    auto t = c.theme;
    double w = c.width, h = c.height, y = h / 2 - 170;
    hits.clear();
    c.rounded({0, 0, w, h}, {0, 0, 0, .45}, 0);
    c.rounded({24, y, w - 48, 340}, t.surface, 28);
    c.text(tr(en, "应用权限", "App permission"), {48, y + 20, w - 96, 48}, 26, t.text, true);
    std::string name = permission.value("app", "");
    for (const auto &a : apps)
        if (a.at("id") == name)
            name = a.value("name", name);
    c.text(name, {48, y + 78, w - 96, 44}, 18, t.text);
    if (permission.value("file_request", false)) {
        y = h / 2 - 280;
        c.rounded({24, y, w - 48, 560}, t.surface, 28);
        c.text(tr(en, "选择要导入的文件", "Choose a file to import"), {44, y + 16, w - 88, 42}, 23,
               t.text, true);
        c.text(picker_directory.empty() ? "Shared" : picker_directory, {44, y + 64, w - 88, 32}, 14,
               t.muted);
        button(c, hits, {44, y + 106, 56, 40}, "↑", [this] {
            auto slash = picker_directory.rfind('/');
            picker_open(slash == std::string::npos ? "" : picker_directory.substr(0, slash));
        });
        button(c, hits, {w - 144, y + 106, 44, 40}, "‹", [this] {
            if (picker_page)
                --picker_page;
        });
        button(c, hits, {w - 92, y + 106, 44, 40}, "›", [this] {
            if ((picker_page + 1) * 6 < picker_files.size())
                ++picker_page;
        });
        for (size_t row = 0; row < 6; ++row) {
            size_t index = picker_page * 6 + row;
            if (index >= picker_files.size())
                break;
            const auto &entry = picker_files[index];
            std::string name = entry.at("name");
            bool directory = entry.at("directory");
            std::string relative = picker_directory.empty() ? name : picker_directory + "/" + name;
            button(c, hits, {44, y + 154 + row * 50, w - 88, 44},
                   (directory ? "▣  " : "·  ") + name, [this, relative, directory] {
                       if (directory)
                           picker_open(relative);
                       else
                           picker_choose(relative);
                   });
        }
        if (picker_files.empty())
            c.text(tr(en, "此目录为空", "This folder is empty"), {44, y + 180, w - 88, 64}, 18,
                   t.muted);
        button(c, hits, {44, y + 494, w - 88, 48}, tr(en, "取消", "Cancel"),
               [this] { permission_decide(false); });
        return;
    }
    bool network = permission.value("permission", "") == "network";
    c.text(network ? tr(en, "允许使用网络？", "Allow network access?")
                   : tr(en, "允许导入选定的文件？", "Allow selected file imports?"),
           {48, y + 128, w - 96, 48}, 18, t.text);
    button(
        c, hits, {44, y + 208, w - 88, 48}, tr(en, "允许", "Allow"),
        [this] { permission_decide(true); }, true);
    button(c, hits, {44, y + 268, w - 88, 48}, tr(en, "拒绝", "Deny"),
           [this] { permission_decide(false); });
}
} // namespace leon::shell
