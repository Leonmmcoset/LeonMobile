#pragma once
#include "package.hpp"
#include <algorithm>
#include <functional>
#include <lmui/lmui.hpp>
#include <string>
#include <vector>
namespace mobile {
using namespace lmui;
using leon::Json;
struct Hit {
    Rect rect;
    std::function<void()> action;
};
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
    hits.push_back({r, std::move(action)});
}
inline void button(Canvas &c, std::vector<Hit> &hits, Rect r, const std::string &text,
                   std::function<void()> action, bool filled = false) {
    c.rounded(r, filled ? c.theme.primary : c.theme.container, 24);
    c.text(text, r, 16, filled ? c.theme.on_primary : c.theme.primary, true, 1);
    click(hits, r, std::move(action));
}
inline void activate(std::vector<Hit> hits, double x, double y) {
    for (auto i = hits.rbegin(); i != hits.rend(); ++i)
        if (i->rect.contains(x, y)) {
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
