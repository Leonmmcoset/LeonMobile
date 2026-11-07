#pragma once
#include "directory.hpp"
#include <leon/mobile/application.hpp>
#include <map>
namespace leon::apps {
class FilesApplication final : public mobile::ApplicationWindow {
  public:
    FilesApplication();

  private:
    Directory directory;
    std::string file_input{"new.txt"};
    std::map<std::string, std::string> transfers;
    unsigned transfer_ticks{};
    void on_tick() override;
    mobile::Json save_state() const override {
        return {{"path", directory.path.string()}, {"input", file_input}};
    }
    void restore_state(const mobile::Json &state) override {
        auto path = state.at("path").get<std::string>();
        file_input = state.at("input");
        if (mobile::fs::is_directory(path))
            directory.path = path;
    }
    void text_input(const std::string &text) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
    bool can_scroll(double, double y) const override { return y >= 230; }
};
} // namespace leon::apps
