#include "application.hpp"
RootDemo::RootDemo() {
    app.paint = [this](lmui::Canvas &canvas) { paint(canvas); };
    app.on_pointer_event([this](const lmui::PointerEvent &event) { touch.handle(event, actions); });
    app.on_lifecycle([this](lmui::LifecycleEvent event) {
        if (event == lmui::LifecycleEvent::shutdown)
            stop.request_stop();
        return true;
    });
    app.tick = [this] {
        if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            result = job.get();
            touch.cancel();
        }
        app.redraw();
    };
}
RootDemo::~RootDemo() {
    stop.request_stop();
    if (job.valid())
        job.wait();
}
void RootDemo::request() {
    if (job.valid())
        return;
    attempted = true;
    result = {};
    stop = std::stop_source{};
    phase = leon::RootPhase::pending;
    job = std::async(std::launch::async, [this, token = stop.get_token()] {
        return leon::request_root({"inspect"}, token,
                                  [this](leon::RootPhase next) { phase = next; });
    });
    app.redraw();
}
int RootDemo::run() { return app.run(); }
