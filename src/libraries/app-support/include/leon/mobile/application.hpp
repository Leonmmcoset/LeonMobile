#pragma once
#include <leon/mobile/keyboard.hpp>
namespace mobile {
// Internal built-in application support. Application state stays in each app.
class ApplicationWindow {
  public:
    ApplicationWindow(std::string id, std::string chinese, std::string english);
    virtual ~ApplicationWindow() = default;
    int run();

  protected:
    Application app;
    std::vector<Hit> hits;
    std::string error;
    Json prefs = {
        {"dark", false}, {"language", "zh-CN"}, {"navigation", "gestures"}, {"volume", 70}};
    bool en{}, keyboard{}, theme_override{};
    double scroll{};
    TouchInput touch_;
    virtual void paint_header(Canvas &canvas);
    virtual void paint_content(Canvas &canvas, double bottom) = 0;
    virtual void text_input(const std::string &) {}
    virtual bool key_event(const KeyEvent &) { return false; }
    virtual void on_tick() {}
    virtual std::string enter_text() const { return "\n"; }
    virtual bool can_scroll(double, double) const { return false; }
    void transition_content(double dx = 32, double dy = 0);
    void perform(const std::function<void()> &action);
    void poll_preferences();
    void set(std::string key, Json value);
    static void edit(std::string &target, const std::string &text);

  private:
    std::string chinese_, english_;
    OnScreenKeyboard keyboard_;
    ContentTransition content_transition_;
    unsigned content_revision_{};
    std::string shown_error_;
    double scroll_start_{}, down_y_{}, scroll_velocity_{}, scroll_last_{}, scroll_time_{};
    bool scroll_eligible_{}, scroll_dragging_{}, reduced_motion_{};
    void paint(Canvas &canvas);
    void pointer(const PointerEvent &event);
};
} // namespace mobile
