#include "application.hpp"
#include <leon/system.hpp>
using namespace mobile;
namespace leon::apps {
EditorApplication::EditorApplication()
    : ApplicationWindow("org.leon.editor", "文本编辑器", "Text editor") {
    document.path = user_home() + "/note.txt";
}
void EditorApplication::import_document() {
    auto result = system_request("portal", {{"op", "request"}, {"permission", "files.import"}});
    awaiting_import = true;
    import_token = 0;
    keyboard = false;
    error = tr(en, "请在系统窗口确认并选择文件", "Confirm and choose a file in the system window");
}
void EditorApplication::on_tick() {
    if (!awaiting_import)
        return;
    try {
        if (!import_token) {
            auto permission =
                system_request("portal", {{"op", "query"}, {"permission", "files.import"}});
            if (!permission.value("granted", false)) {
                if (!permission.value("pending", false)) {
                    awaiting_import = false;
                    error = tr(en, "文件访问未获授权", "File access was not granted");
                    app.redraw();
                }
                return;
            }
            import_token = system_request("portal", {{"op", "choose"}}).at("token");
        }
        auto result = system_request("portal", {{"op", "selection"}, {"token", import_token}});
        if (result.value("done", false)) {
            awaiting_import = false;
            error.clear();
            auto path = result.value("path", "");
            if (!path.empty()) {
                document.path = path;
                document.load();
                focus = 0;
            }
            app.redraw();
        }
    } catch (const std::exception &e) {
        awaiting_import = false;
        error = e.what();
        app.redraw();
    }
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
        c, hits, {20, bottom - 60, w - 152, 48},
        tr(en, "保存到应用目录", "Save to application directory"),
        [&] { perform([&] { document.save(); }); }, true);
    button(c, hits, {w - 120, bottom - 60, 100, 48}, tr(en, "导入", "Import"),
           [&] { perform([&] { import_document(); }); });
}
} // namespace leon::apps
