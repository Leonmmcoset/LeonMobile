#pragma once
#include "document.hpp"
#include <future>
#include <leon/mobile/application.hpp>
namespace leon::apps {
class EditorApplication final : public mobile::ApplicationWindow {
  public:
    EditorApplication();

  private:
    Document document;
    std::future<mobile::Json> document_job;
    std::string document_token;
    unsigned interaction_ticks{};
    void poll_interactions();
    int focus{};
    bool awaiting_import{};
    long long import_token{};
    void import_document();
    void on_tick() override;
    mobile::Json save_state() const override {
        return {{"path", document.path}, {"text", document.text}};
    }
    void restore_state(const mobile::Json &state) override {
        auto path = state.at("path").get<std::string>();
        auto text = state.at("text").get<std::string>();
        document.path = std::move(path);
        document.text = std::move(text);
    }
    void text_input(const std::string &text) override;
    void paint_content(mobile::Canvas &canvas, double bottom) override;
};
} // namespace leon::apps
