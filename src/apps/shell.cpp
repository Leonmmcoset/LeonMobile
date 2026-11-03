#include "app_common.hpp"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>
using namespace mobile;
enum class Page { lock, home, apps, tasks, quick, notifications, active };
static bool wm(const std::string &command) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, "/run/leon/wm.sock");
    bool ok = !connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
    if (ok)
        send(fd, command.data(), command.size(), MSG_NOSIGNAL);
    close(fd);
    return ok;
}
int main() {
    Application app("org.leon.shell", "LeonMobile");
    app.transparent = true;
    Page page = Page::lock;
    std::vector<Hit> hits;
    Json apps = Json::array(), running = Json::object(), notifications = Json::array();
    std::string error, active_id;
    Json prefs = {
        {"dark", false}, {"language", "zh-CN"}, {"navigation", "gestures"}, {"volume", 70}};
    bool en = false;
    int tick = 0;
    double down_x = 0, down_y = 0;
    bool pressed = false;
    auto refresh = [&] {
        try {
            auto s = rpc({{"op", "list"}});
            apps = s.at("apps");
            running = s.at("running");
            prefs = rpc({{"op", "settings"}}).at("settings");
            notifications = rpc({{"op", "notifications"}}).at("notifications");
            app.dark = prefs.value("dark", false);
            en = prefs["language"] == "en";
            error.clear();
        } catch (const std::exception &e) {
            error = e.what();
        }
    };
    refresh();
    auto home = [&] {
        page = Page::home;
        active_id.clear();
        wm("shell");
        app.redraw();
    };
    auto launch = [&](std::string id) {
        try {
            rpc({{"op", "launch"}, {"id", id}});
            active_id = id;
            page = Page::active;
            wm("focus " + id);
            refresh();
        } catch (const std::exception &e) {
            error = e.what();
        }
        app.redraw();
    };
    app.tick = [&] {
        if (++tick % 5 == 0) {
            refresh();
            if (page == Page::active && !active_id.empty() && !running.contains(active_id))
                home();
            app.redraw();
        }
    };
    app.key = [&](const KeyEvent &e) {
        if (e.pressed && e.symbol == XKB_KEY_Escape)
            home();
    };
    app.pointer = [&](double x, double y, bool down) {
        if (down) {
            pressed = true;
            down_x = x;
            down_y = y;
            return;
        }
        if (!pressed)
            return;
        pressed = false;
        double dx = x - down_x, dy = y - down_y;
        if (page == Page::lock) {
            if (dy < -60 || std::abs(dx) < 12)
                home();
            return;
        }
        if (down_y < 42 && dy > 55) {
            page = x > app.width() / 2 ? Page::quick : Page::notifications;
            wm("shell");
            app.redraw();
            return;
        }
        if (prefs["navigation"] == "gestures" && down_y > app.height() - 64 && dy < -50) {
            if (dy < -190) {
                page = Page::tasks;
                wm("shell");
            } else
                home();
            app.redraw();
            return;
        }
        if (std::abs(dx) < 16 && std::abs(dy) < 16) {
            try {
                activate(hits, x, y);
            } catch (const std::exception &e) {
                error = e.what();
            }
        }
        app.redraw();
    };
    app.paint = [&](Canvas &c) {
        hits.clear();
        double w = c.width, h = c.height;
        auto t = c.theme;
        bool overlay = page != Page::active;
        if (overlay)
            c.clear(t.background);
        c.rounded({0, 0, w, 42}, t.background, 0);
        c.rounded({0, h - 64, w, 64}, t.background, 0);
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char time[16];
        std::strftime(time, sizeof(time), "%H:%M", &tm);
        c.text(time, {24, 4, 100, 32}, 14, t.text, true);
        bool connected = false;
        std::ifstream state("/sys/class/net/eth0/operstate");
        std::string net;
        state >> net;
        connected = net == "up";
        c.text(connected ? "ETH  ·  AC" : "—  ·  AC", {w - 180, 4, 156, 32}, 12, t.muted, false, 2);
        click(hits, {0, 0, w / 2, 42}, [&] {
            if (page != Page::lock) {
                page = Page::notifications;
                wm("shell");
            }
        });
        click(hits, {w / 2, 0, w / 2, 42}, [&] {
            if (page != Page::lock) {
                page = Page::quick;
                wm("shell");
            }
        });
        if (page == Page::lock) {
            c.circle(w * .75, h * .45, w * .6, t.container);
            c.text(time, {24, h * .22, w - 48, 130}, 92, t.primary, false, 1);
            c.text("LeonMobile", {24, h * .22 + 140, w - 48, 50}, 24, t.text, true, 1);
            c.text(tr(en, "向上滑动解锁", "Swipe up to unlock"), {24, h - 180, w - 48, 48}, 16,
                   t.muted, false, 1);
        } else if (page == Page::home || page == Page::apps) {
            if (page == Page::home) {
                c.circle(w * .88, 220, 180, t.container);
                c.text(tr(en, "你好，Leon", "Hello, Leon"), {28, 76, w - 56, 56}, 36, t.text, true);
                c.text(tr(en, "你的移动空间", "Your mobile space"), {28, 136, w - 56, 36}, 17,
                       t.muted);
                c.rounded({24, 210, w - 48, 124}, t.primary, 32);
                c.text("LeonMobile", {44, 228, w - 88, 48}, 30, t.on_primary, true);
                c.text(tr(en, "从这里，开始探索。", "A little space to explore."),
                       {44, 278, w - 88, 34}, 16, t.on_primary);
            } else
                c.text(tr(en, "所有应用", "All apps"), {28, 68, w - 56, 64}, 32, t.text, true);
            double top = page == Page::home ? 378 : 160;
            size_t limit = page == Page::home ? std::min<size_t>(6, apps.size()) : apps.size();
            double cell = (w - 48) / 3;
            std::vector<unsigned> palette = {0xeaddff, 0xd1e8d5, 0xffdcc4, 0xd8e2ff,
                                             0xf4d9f2, 0xcde9e8, 0xffdf9e, 0xe2e1ee};
            for (size_t i = 0; i < limit; ++i) {
                const auto &m = apps[i];
                double x = 24 + (i % 3) * cell, y = top + (i / 3) * 118;
                std::string id = m.at("id");
                std::string name = en ? m.value("name_en", m.at("name").get<std::string>())
                                      : m.at("name").get<std::string>();
                c.rounded({x + cell / 2 - 30, y, 60, 60},
                          app.dark ? t.container : Color::hex(palette[i % palette.size()]), 20);
                std::string symbol = m.value("icon", "");
                symbol = symbol == "settings"     ? "⚙"
                         : symbol == "calculator" ? "="
                         : symbol == "files"      ? "▣"
                         : symbol == "terminal"   ? ">_"
                         : symbol == "clock"      ? "◷"
                         : symbol == "editor"     ? "✎"
                         : symbol == "images"     ? "▧"
                                                  : "◈";
                c.text(symbol, {x + cell / 2 - 30, y, 60, 60}, 28, t.primary, true, 1);
                c.text(name, {x, y + 66, cell, 34}, 13, t.text, false, 1);
                click(hits, {x, y, cell, 104}, [&, id] { launch(id); });
            }
            if (page == Page::home)
                button(c, hits, {48, h - 148, w - 96, 52}, tr(en, "所有应用  ↑", "All apps  ↑"),
                       [&] { page = Page::apps; });
        } else if (page == Page::tasks) {
            c.text(tr(en, "最近任务", "Recent tasks"), {24, 70, w - 48, 64}, 32, t.text, true);
            double y = 158;
            for (const auto &m : apps) {
                std::string id = m.at("id");
                if (!running.contains(id))
                    continue;
                std::string name = en ? m.value("name_en", "") : m.at("name").get<std::string>();
                button(c, hits, {24, y, w - 104, 78}, name, [&, id] {
                    active_id = id;
                    page = Page::active;
                    wm("focus " + id);
                });
                button(c, hits, {w - 68, y, 44, 78}, "×", [&, id] {
                    rpc({{"op", "stop"}, {"id", id}});
                    refresh();
                });
                y += 94;
            }
            if (running.empty())
                c.text(tr(en, "暂无运行中的应用", "No running applications"), {24, 200, w - 48, 80},
                       18, t.muted, false, 1);
        } else if (page == Page::quick) {
            c.text(tr(en, "快捷设置", "Quick settings"), {24, 70, w - 48, 64}, 32, t.text, true);
            button(c, hits, {24, 164, w - 48, 76},
                   connected ? tr(en, "以太网 · 已连接", "Ethernet · Connected")
                             : tr(en, "以太网 · 未连接", "Ethernet · Disconnected"),
                   [] {});
            button(c, hits, {24, 256, w - 48, 64},
                   app.dark ? tr(en, "深色模式 · 开", "Dark theme · On")
                            : tr(en, "深色模式 · 关", "Dark theme · Off"),
                   [&] {
                       rpc({{"op", "set"}, {"key", "dark"}, {"value", !app.dark}});
                       refresh();
                   });
            button(c, hits, {24, 336, w - 48, 64}, tr(en, "锁定屏幕", "Lock screen"), [&] {
                page = Page::lock;
                wm("shell");
            });
            button(c, hits, {24, 416, w - 48, 64}, tr(en, "打开设置", "Open settings"),
                   [&] { launch("org.leon.settings"); });
            button(c, hits, {24, 496, w - 48, 64}, tr(en, "关机", "Power off"), [&] {
                rpc({{"op", "poweroff"}});
            });
        } else if (page == Page::notifications) {
            c.text(tr(en, "通知", "Notifications"), {24, 70, w - 48, 64}, 32, t.text, true);
            double y = 160;
            if (notifications.empty())
                c.text(tr(en, "暂时没有新通知", "You're all caught up"), {24, 220, w - 48, 80}, 20,
                       t.muted, false, 1);
            for (const auto &n : notifications) {
                c.rounded({24, y, w - 48, 84}, t.container, 24);
                c.text(n.at("title"), {40, y + 10, w - 80, 40}, 16, t.text);
                c.text(n.at("app"), {40, y + 48, w - 80, 24}, 11, t.muted);
                y += 96;
                if (y > h - 160)
                    break;
            }
        }
        if (page != Page::lock) {
            if (prefs["navigation"] == "buttons") {
                button(c, hits, {16, h - 58, (w - 48) / 3, 48}, "‹", home);
                button(c, hits, {24 + (w - 48) / 3, h - 58, (w - 48) / 3, 48}, "○", home);
                button(c, hits, {32 + 2 * (w - 48) / 3, h - 58, (w - 48) / 3, 48}, "□", [&] {
                    page = Page::tasks;
                    wm("shell");
                });
            } else {
                c.rounded({w / 2 - 60, h - 24, 120, 5}, t.text, 3);
                click(hits, {w / 3, h - 64, w / 3, 64}, home);
                click(hits, {0, h - 64, w / 3, 64}, [&] {
                    page = Page::tasks;
                    wm("shell");
                });
                click(hits, {w * 2 / 3, h - 64, w / 3, 64}, [&] {
                    page = Page::apps;
                    wm("shell");
                });
            }
        }
        if (!error.empty() && overlay) {
            c.text(tr(en, "系统服务暂未就绪", "System services are starting"),
                   {20, h - 200, w - 40, 44}, 13, t.muted);
        }
        app.input_region(overlay ? std::vector<Rect>{{0, 0, w, h}}
                                 : std::vector<Rect>{{0, 0, w, 42}, {0, h - 64, w, 64}});
    };
    return app.run();
}
