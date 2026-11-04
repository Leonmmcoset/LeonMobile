#pragma once
#include "shade.hpp"
#include <leon/mobile/ui.hpp>
namespace leon::shell {
enum class Page { lock, home, apps, tasks, active };
class Shell {
  public:
    Shell();
    int run();

  private:
    lmui::Application app{"org.leon.shell", "LeonMobile"};
    Page page{Page::lock};
    std::vector<mobile::Hit> hits;
    leon::Json apps = Json::array(), running = Json::object(), notifications = Json::array();
    leon::Json prefs = {
        {"dark", false}, {"language", "zh-CN"}, {"navigation", "gestures"}, {"volume", 70}};
    std::string error, active_id;
    bool en{}, connected{}, pressed{}, notifications_available{};
    Shade shade;
    bool shade_gesture{}, shade_focused{}, volume_gesture{}, notification_gesture{}, power_menu{};
    int volume_preview{-1}, unmuted_volume{70};
    double notification_scroll{}, notification_start{}, notification_limit{};
    mobile::Rect volume_track{}, notification_bounds{};
    int ticks{};
    double down_x{}, down_y{};
    double lock_drag{};
    lmui::TouchInput touch_feedback;
    lmui::ContentTransition page_transition;
    Page presented_page{Page::lock};
    void refresh();
    void home();
    void launch(std::string id);
    void pointer(double x, double y, bool down);
    void motion(double x, double y);
    void open_shade(Shade::Level level);
    void close_shade();
    void reset_shade();
    void shade_click(mobile::Rect bounds, std::function<void()> action, bool prominent = false);
    void set_preference(const std::string &key, leon::Json value);
    void toggle_mute();
    void paint(lmui::Canvas &canvas);
    void paint_status(lmui::Canvas &canvas, const std::string &time);
    void paint_lock(lmui::Canvas &canvas, const std::string &time);
    void paint_home(lmui::Canvas &canvas);
    void paint_tasks(lmui::Canvas &canvas);
    void paint_shade(lmui::Canvas &canvas, const std::string &time);
    void paint_quick(lmui::Canvas &canvas, double expansion);
    void paint_notifications(lmui::Canvas &canvas, double top, double bottom);
    void paint_power_menu(lmui::Canvas &canvas);
    void paint_navigation(lmui::Canvas &canvas);
};
} // namespace leon::shell
