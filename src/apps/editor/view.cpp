#include "application.hpp"
using namespace mobile;
namespace leon::apps {
EditorApplication::EditorApplication()
    : ApplicationWindow("org.leon.editor", "文本编辑器", "Text editor") {
    document.path = user_home() + "/note.txt";
}
void EditorApplication::text_input(const std::string &text) {
    edit(focus == 1 ? document.path : document.text, text);
}
void EditorApplication::paint_content(Canvas &c, double bottom) {
    auto t = c.theme;
    double w = c.width;
    button(c, hits, {20, 84, w - 132, 44}, document.path, [&] {
        focus = 1;
        keyboard = true;
    });
    button(c, hits, {w - 104, 84, 84, 44}, tr(en, "打开", "Open"), [&] {
        perform([&] {
            document.load();
            focus = 0;
        });
    });
    c.rounded({20, 144, w - 40, bottom - 222}, t.surface, 16);
    c.text(document.text.empty() ? tr(en, "点击开始编辑…", "Tap to edit…") : document.text,
           {32, 154, w - 64, bottom - 242}, 16, t.text);
    click(hits, {20, 144, w - 40, bottom - 222}, [&] {
        focus = 0;
        keyboard = true;
    });
    button(
        c, hits, {20, bottom - 60, w - 40, 48},
        tr(en, "保存到应用目录", "Save to application directory"),
        [&] { perform([&] { document.save(); }); }, true);
}
} // namespace leon::apps
