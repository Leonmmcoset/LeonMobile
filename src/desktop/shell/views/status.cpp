#include "../shell.hpp"
#include <fstream>
using namespace mobile;
namespace leon::shell {
void Shell::paint_status(Canvas &c, const std::string &time) {
    auto t = c.theme;
    double w = c.width, h = c.height;
    c.rounded({0, 0, w, 42}, t.background, 0);
    c.rounded({0, h - 64, w, 64}, t.background, 0);
    c.text(time, {24, 4, 100, 32}, 14, t.text, true);
    connected = false;
    std::ifstream state("/sys/class/net/eth0/operstate");
    std::string net;
    state >> net;
    connected = net == "up";
    c.text(connected ? "ETH  ·  AC" : "—  ·  AC", {w - 180, 4, 156, 32}, 12, t.muted, false, 2);
    click(hits, {0, 0, w, 42}, [this] { open_shade(Shade::Level::compact); });
}
} // namespace leon::shell
