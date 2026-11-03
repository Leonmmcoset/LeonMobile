#include <algorithm>
#include <lmui/lmui.hpp>
int main() {
    lmui::Application app("org.example.hello", "Hello LeonMobile");
    int count = 0;
    lmui::Widget tree;
    app.paint = [&](lmui::Canvas &c) {
        c.text("Hello, LeonMobile", {24, 48, double(c.width - 48), 80}, 30, c.theme.text, true);
        tree = lmui::Column({lmui::Label("原生 ELF · musl · LMUI"),
                             lmui::Button("Clicked " + std::to_string(count), [&] { ++count; })});
        tree.layout({24, 180, double(c.width - 48), 180});
        tree.draw(c);
    };
    app.pointer = [&](double x, double y, bool pressed) {
        if (pressed) {
            if (const auto *widget = tree.hit(x, y); widget && widget->activate)
                widget->activate();
            app.redraw();
        }
    };
    return app.run();
}
