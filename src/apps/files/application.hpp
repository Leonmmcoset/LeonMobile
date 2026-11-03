#pragma once
#include "directory.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class FilesApplication final : public mobile::ApplicationWindow {
  public:
    FilesApplication();

  private:
    Directory directory;
    std::string file_input{"new.txt"};
    void text_input(const std::string &text) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
