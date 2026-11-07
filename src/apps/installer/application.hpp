#pragma once
#include <future>
#include <leon/mobile/application.hpp>

namespace leon::apps {
class InstallerApplication final : public mobile::ApplicationWindow {
  public:
    InstallerApplication();

  private:
    mobile::Json transaction{{"state", "idle"}};
    std::future<mobile::Json> job;
    std::string source, picker, activity;
    unsigned ticks{};
    bool initialized{};
    void request(mobile::Json message);
    void choose();
    void import_package(const std::string &document, const std::string &event = "");
    void cancel();
    void on_tick() override;
    void paint_content(mobile::Canvas &, double bottom) override;
    bool can_scroll(double, double y) const override { return y > 80 && y < app.height() - 96; }
    bool defer_preferences() const override { return job.valid(); }
};
} // namespace leon::apps
