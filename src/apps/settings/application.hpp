#pragma once
#include "auth.h"
#include "catalog.hpp"
#include "device.hpp"
#include <future>
#include <leon/mobile/application.hpp>
namespace leon::apps {
class SettingsApplication final : public mobile::ApplicationWindow {
  public:
    SettingsApplication();
    ~SettingsApplication() override;

  private:
    settings::Page page_{settings::Page::home};
    std::vector<settings::Page> history_;
    settings::DeviceInfo device_;
    std::string query_;
    mobile::Rect viewport_;
    unsigned status_ticks_{};
    int volume_preview_{-1};
    enum class DeveloperView { home, kernel, modules, services, core };
    DeveloperView developer_view_{DeveloperView::home};
    mobile::Json root_status_ = {{"enabled", false}, {"password_set", false}};
    mobile::Json developer_data_ = mobile::Json::object(), pending_root_ = mobile::Json::object();
    std::string developer_path_, developer_selection_, developer_value_, developer_message_;
    std::string root_password_, root_new_password_, root_confirmation_;
    int developer_offset_{}, developer_field_{-1};
    bool root_dialog_{}, root_new_required_{}, root_current_required_{}, modules_available_{};
    void paint_header(mobile::Canvas &canvas) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
    void text_input(const std::string &text) override;
    bool key_event(const mobile::KeyEvent &event) override;
    bool private_text_input() const override { return root_dialog_ || security_dialog_; }
    bool defer_preferences() const override { return applications_job_.valid(); }
    bool can_scroll(double x, double y) const override { return viewport_.contains(x, y); }
    void on_tick() override;
    void open(settings::Page page);
    void back();
    void save_preference(const std::string &key, mobile::Json value);
    std::string title(settings::Page page) const;
    std::string summary(settings::Page page) const;
    std::vector<settings::Page> visible_entries() const;
    void content_hit(mobile::Rect bounds, std::function<void(double, double)> action);
    void link(mobile::Canvas &canvas, mobile::Rect bounds, settings::Page page);
    void paint_home(mobile::Canvas &canvas);
    void paint_preferences(mobile::Canvas &canvas);
    void paint_device(mobile::Canvas &canvas);
    mobile::Json applications_ = mobile::Json::array(), application_ = mobile::Json::object();
    std::future<mobile::Json> applications_job_;
    std::string applications_query_, application_id_, applications_operation_, applications_subject_;
    std::string applications_confirmation_, applications_message_;
    bool applications_loaded_{}, applications_failed_{};
    unsigned applications_ticks_{};
    void applications_start(const std::string &operation, const std::string &id = {});
    void applications_poll();
    void applications_refresh();
    void applications_open(const std::string &id);
    bool applications_back();
    void applications_text(const std::string &text);
    void applications_confirm(const std::string &operation);
    std::vector<mobile::Json> visible_applications() const;
    double applications_bottom() const;
    void paint_applications(mobile::Canvas &canvas);
    auth_state security_status_{};
    std::future<auth_state> security_job_;
    std::string device_current_, device_new_, device_confirm_, security_kind_, security_message_;
    bool security_dialog_{};
    int security_field_{};
    void security_refresh();
    void security_begin(const std::string &kind);
    void security_submit();
    void security_text(const std::string &text);
    void clear_device_fields();
    void paint_security(mobile::Canvas &canvas);
    void paint_developer(mobile::Canvas &canvas);
    void developer_refresh();
    void developer_back();
    void developer_auth(mobile::Json operation);
    void developer_submit();
    bool developer_text(const std::string &text);
    void clear_root_fields();
    mobile::Json root_request(mobile::Json request);
};
} // namespace leon::apps
