#include "wayland/backend.hpp"
#include <iostream>
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
void Application::input_region(const std::vector<Rect> &rectangles) {
    if (!impl->surface)
        return;
    auto *region = wl_compositor_create_region(impl->compositor);
    for (auto r : rectangles)
        wl_region_add(region, int(r.x), int(r.y), int(r.w), int(r.h));
    wl_surface_set_input_region(impl->surface, region);
    wl_region_destroy(region);
}
int Application::width() const { return impl->w; }
int Application::height() const { return impl->h; }
} // namespace lmui
