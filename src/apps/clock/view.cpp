#include "application.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>
using namespace mobile;
namespace leon::apps {
ClockApplication::ClockApplication() : ApplicationWindow("org.leon.clock", "时钟", "Clock") {}
void ClockApplication::on_tick() { app.redraw(); }
void ClockApplication::paint_content(Canvas &c, double) {
    auto t = c.theme;
    double w = c.width;
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char date[64];
    auto time=clock_text(tm,prefs.value("clock_24h",true),true);
    std::strftime(date, sizeof(date), "%Y-%m-%d", &tm);
    c.text(time, {20, 160, w - 40, 110}, prefs.value("clock_24h",true)?64:48, t.primary, false, 1);
    c.text(date, {20, 276, w - 40, 44}, 20, t.muted, false, 1);
    double seconds = stopwatch.seconds();
    std::ostringstream s;
    s << std::fixed << std::setprecision(1) << seconds << " s";
    c.text(s.str(), {20, 380, w - 40, 70}, 36, t.text, false, 1);
    button(
        c, hits, {24, 474, (w - 64) / 2, 52},
        stopwatch.running() ? tr(en, "暂停", "Pause") : tr(en, "秒表", "Stopwatch"),
        [&] { stopwatch.toggle(); }, true);
    button(c, hits, {40 + (w - 64) / 2, 474, (w - 64) / 2, 52}, tr(en, "复位", "Reset"),
           [&] { stopwatch.reset(); });
}
} // namespace leon::apps
