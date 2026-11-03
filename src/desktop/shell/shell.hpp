#pragma once
#include <leon/mobile/ui.hpp>
namespace leon::shell {
enum class Page { lock, home, apps, tasks, quick, notifications, active };
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
    bool en{}, connected{}, pressed{};
    int ticks{};
    double down_x{}, down_y{};
    void refresh();
    void home();
    void launch(std::string id);
    void pointer(double x, double y, bool down);
    void paint(lmui::Canvas &canvas);
    void paint_status(lmui::Canvas &canvas, const std::string &time);
    void paint_lock(lmui::Canvas &canvas, const std::string &time);
    void paint_home(lmui::Canvas &canvas);
    void paint_tasks(lmui::Canvas &canvas);
    void paint_quick(lmui::Canvas &canvas);
    void paint_notifications(lmui::Canvas &canvas);
    void paint_navigation(lmui::Canvas &canvas);
};
} // namespace leon::shell
