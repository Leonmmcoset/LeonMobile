#pragma once
#include <functional>
#include <lmui/input.hpp>
#include <lmui/types.hpp>
#include <memory>
#include <string>
#include <vector>
namespace lmui {
class Canvas;
enum class LifecycleEvent { foreground, background, save, shutdown };
struct KeyEvent {
    unsigned symbol{};
    std::string text;
    bool pressed{};
};
class Application {
  public:
    Application(std::string id, std::string title);
    ~Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    std::function<void(Canvas &)> paint;
    std::function<void(double, double, bool)> pointer;
    std::function<void(double)> scroll;
    std::function<void(const KeyEvent &)> key;
    std::function<void()> tick;
    bool dark{};
    bool transparent{};
    int run();
    void redraw();
    void quit();
    void input_region(const std::vector<Rect> &rectangles);
    void on_pointer_motion(std::function<void(double, double)> callback);
    void on_pointer_event(std::function<void(const PointerEvent &)> callback);
    void reduce_motion(bool enabled);
    bool reduced_motion() const;
    void on_lifecycle(std::function<bool(LifecycleEvent)> callback);
    // Optional Wayland text-input-v3 integration; callbacks run on the UI thread.
    bool text_input_available() const;
    void text_input(bool enabled);
    void on_text_input(std::function<void(const std::string &)> commit,
                       std::function<void(const std::string &)> preedit = {},
                       std::function<void(uint32_t, uint32_t)> erase = {});
    // Readable/error callbacks execute on the UI thread; the caller owns the FD.
    void watch_fd(int fd, std::function<void()> callback);
    void unwatch_fd(int fd);
    int width() const;
    int height() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lmui
