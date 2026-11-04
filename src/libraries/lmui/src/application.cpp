#include "wayland/backend.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace lmui {
Application::Application(std::string id, std::string title)
    : impl(std::make_unique<Impl>(this, std::move(id), std::move(title))) {}
Application::~Application() = default;
int Application::run() {
    try {
        return impl->run();
    } catch (const std::exception &e) {
        std::cerr << "LMUI: " << e.what() << '\n';
        return 1;
    }
}
void Application::redraw() { impl->dirty = true; }
void Application::quit() { impl->running = false; }
void Application::on_lifecycle(std::function<bool(LifecycleEvent)> callback) {
    impl->lifecycle_callback = std::move(callback);
}
void Application::on_pointer_motion(std::function<void(double, double)> callback) {
    impl->motion_callback = std::move(callback);
}
void Application::on_pointer_event(std::function<void(const PointerEvent &)> callback) {
    impl->event_callback = std::move(callback);
}
void Application::reduce_motion(bool enabled) {
    if (impl->reduce_motion != enabled) {
        impl->reduce_motion = enabled;
        redraw();
    }
}
bool Application::reduced_motion() const { return impl->reduce_motion || impl->force_reduced; }
void Application::input_region(const std::vector<Rect> &rectangles) {
    if (!impl->surface)
        return;
    auto *region = wl_compositor_create_region(impl->compositor);
    for (auto r : rectangles)
        wl_region_add(region, int(r.x), int(r.y), int(r.w), int(r.h));
    wl_surface_set_input_region(impl->surface, region);
    wl_region_destroy(region);
}
void Application::watch_fd(int fd, std::function<void()> callback) {
    if (fd < 0 || !callback)
        throw std::invalid_argument("invalid FD watch");
    unwatch_fd(fd);
    if (impl->watched_fds.size() >= 16)
        throw std::runtime_error("FD watch limit");
    impl->watched_fds.push_back({fd, ++impl->watch_generation, std::move(callback)});
}
void Application::unwatch_fd(int fd) {
    auto &fds = impl->watched_fds;
    fds.erase(
        std::remove_if(fds.begin(), fds.end(), [&](const auto &entry) { return entry.fd == fd; }),
        fds.end());
}
int Application::width() const { return impl->w; }
int Application::height() const { return impl->h; }
} // namespace lmui
