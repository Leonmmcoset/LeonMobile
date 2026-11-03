#pragma once
#include <leon/mobile/application.hpp>
namespace leon::apps {
class SettingsApplication final : public mobile::ApplicationWindow {
  public:
    SettingsApplication();

  private:
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
