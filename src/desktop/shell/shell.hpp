#pragma once
#include "auth.h"
#include "shade.hpp"
#include <future>
#include <leon/mobile/ui.hpp>
#include <leon/system.hpp>
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
    leon::Json app_states = Json::object();
    leon::Json prefs = {
        {"dark", false}, {"language", "zh-CN"}, {"navigation", "gestures"}, {"volume", 70}};
    std::string error, active_id, timezone;
    bool background_pending{};
    bool en{}, connected{}, pressed{}, notifications_available{};
    Shade shade;
    bool shade_gesture{}, shade_focused{}, volume_gesture{}, notification_gesture{}, power_menu{};
    int volume_preview{-1}, unmuted_volume{70};
    double notification_scroll{}, notification_start{}, notification_limit{};
    mobile::Rect volume_track{}, notification_bounds{};
    int reported_locked{-1};
    auth_state authentication{};
    std::string device_secret, auth_message, ready_epoch;
    std::future<auth_state> auth_job;
    bool unlock_entry{}, unlock_pending{};
    void authentication_refresh();
    void unlock_start();
    void unlock_submit();
    void unlock_text(const std::string &text);
    void clear_device_secret();
    Json root_prompt = Json::object();
    std::string root_secret, root_message, root_job_token;
    std::future<Json> root_job;
    bool root_credentials{};
    size_t root_detail_page{};
    void root_refresh();
    void root_clear_prompt();
    void root_submit(bool allow, bool block = false);
    void root_cancel();
    void root_text(const std::string &);
    std::vector<std::string> root_command_lines() const;
    void paint_root(lmui::Canvas &);
    Json permission = Json::object(), power_state = Json::object();
    std::string power_label(bool compact = false) const;
    bool permission_focused{};
    Json picker_files = Json::array();
    std::string picker_directory;
    size_t picker_page{};
    void picker_open(const std::string &directory);
    void picker_choose(const std::string &file);
    void system_refresh();
    void paint_permission(lmui::Canvas &);
    void permission_decide(bool allow);
    int ticks{};
    double down_x{}, down_y{};
    double lock_drag{};
    lmui::TouchInput touch_feedback;
    lmui::ContentTransition page_transition;
    Page presented_page{Page::lock};
    void refresh();
    void synchronize_background();
    void show_page(Page destination);
    void home();
    void launch(std::string id);
    void dismiss_task(const std::string &id);
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
