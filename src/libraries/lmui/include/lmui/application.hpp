#pragma once
#include <functional>
#include <lmui/types.hpp>
#include <memory>
#include <string>
#include <vector>
namespace lmui {
class Canvas;
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
    int width() const;
    int height() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lmui
