#include <algorithm>
#include <lmui/lmui.hpp>
int main() {
    lmui::Application app("org.example.hello", "Hello LeonMobile");
    int count = 0;
    lmui::Widget tree;
    lmui::TouchInput touch;
    app.paint = [&](lmui::Canvas &c) {
        c.text("Hello, LeonMobile", {24, 48, double(c.width - 48), 80}, 30, c.theme.text, true);
        tree = lmui::Column({lmui::Label("原生 ELF · musl · LMUI"),
                             lmui::Button("Clicked " + std::to_string(count), [&] { ++count; }),
                             lmui::Toggle("Dark theme", app.dark, [&] { app.dark = !app.dark; })});
        tree.layout({24, 180, double(c.width - 48), 220});
        tree.draw(c);
        touch.draw(c);
    };
    app.on_pointer_event([&](const lmui::PointerEvent &event) { touch.handle(event, tree); });
    return app.run();
}
