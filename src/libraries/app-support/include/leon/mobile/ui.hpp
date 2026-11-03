#pragma once
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <leon/ipc.hpp>
#include <lmui/lmui.hpp>
#include <string>
#include <vector>
namespace mobile {
namespace fs = std::filesystem;
using namespace lmui;
using leon::Json;
using Hit = lmui::TouchTarget;
inline Json rpc(const Json &message) {
    auto response = Json::parse(leon::request("/run/leon/apps.sock", message));
    if (!response.value("ok", false))
        throw std::runtime_error(response.value("error", "service error"));
    return response;
}
inline std::string tr(bool en, const std::string &zh, const std::string &eng) {
    return en ? eng : zh;
}
inline void click(std::vector<Hit> &hits, Rect r, std::function<void()> action) {
    hits.push_back({r, std::move(action), {}});
}
inline void click(std::vector<Hit> &hits, Rect r, std::function<void(double, double)> action) {
    hits.push_back({r, {}, std::move(action)});
}
inline void button(Canvas &c, std::vector<Hit> &hits, Rect r, const std::string &text,
                   std::function<void()> action, bool filled = false) {
    c.rounded(r, filled ? c.theme.primary : c.theme.container, 24);
    c.text(text, r, 16, filled ? c.theme.on_primary : c.theme.primary, true, 1);
    click(hits, r, std::move(action));
    hits.back().prominent = filled;
}
inline void translate_hits(std::vector<Hit> &hits, size_t first, double dx, double dy, Rect clip) {
    for (size_t i = first; i < hits.size(); ++i) {
        auto &r = hits[i].rect;
        r.x += dx;
        r.y += dy;
        double right = std::min(r.x + r.w, clip.x + clip.w);
        double bottom = std::min(r.y + r.h, clip.y + clip.h);
        r.x = std::max(r.x, clip.x);
        r.y = std::max(r.y, clip.y);
        r.w = std::max(0.0, right - r.x);
        r.h = std::max(0.0, bottom - r.y);
        // Position callbacks consume content coordinates, even during movement.
        if (hits[i].position_action && (dx != 0 || dy != 0)) {
            auto action = hits[i].position_action;
            hits[i].position_action = [action, dx, dy](double x, double y) {
                action(x - dx, y - dy);
            };
        }
    }
}
inline void activate(std::vector<Hit> hits, double x, double y) {
    for (auto i = hits.rbegin(); i != hits.rend(); ++i)
        if (i->rect.contains(x, y)) {
            if (i->position_action)
                i->position_action(x, y);
            else if (i->action)
                i->action();
            break;
        }
}
inline void backspace(std::string &s) {
    if (s.empty())
        return;
    size_t p = s.size() - 1;
    while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xc0) == 0x80)
        --p;
    s.erase(p);
}
inline std::string user_home() {
    const char *home = getenv("HOME");
    return home ? home : "/tmp";
}
} // namespace mobile
