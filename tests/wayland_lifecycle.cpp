#include <iostream>
#include <lmui/lmui.hpp>
int main() {
    lmui::Application app("org.leon.lifecycle-test", "Lifecycle test");
    app.paint = [](lmui::Canvas &canvas) { canvas.clear(canvas.theme.background); };
    app.on_lifecycle([](lmui::LifecycleEvent event) {
        std::cout << int(event) << std::endl;
        return true;
    });
    return app.run();
}
