#pragma once
#include "catalog.hpp"
#include "device.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class SettingsApplication final : public mobile::ApplicationWindow {
  public:
    SettingsApplication();

  private:
    settings::Page page_{settings::Page::home};
    std::vector<settings::Page> history_;
    settings::DeviceInfo device_;
    std::string query_;
    mobile::Rect viewport_;
    unsigned status_ticks_{};
    int volume_preview_{-1};
    void paint_header(mobile::Canvas &canvas) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
    void text_input(const std::string &text) override;
    bool key_event(const mobile::KeyEvent &event) override;
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
};
} // namespace leon::apps
