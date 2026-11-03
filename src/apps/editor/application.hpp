#pragma once
#include "document.hpp"
#include <leon/mobile/application.hpp>
namespace leon::apps {
class EditorApplication final : public mobile::ApplicationWindow {
  public:
    EditorApplication();

  private:
    Document document;
    int focus{};
    void text_input(const std::string &text) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
