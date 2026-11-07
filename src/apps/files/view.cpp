#include "application.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <leon/platform.hpp>
#include <leon/system.hpp>
using namespace mobile;
namespace leon::apps {
FilesApplication::FilesApplication() : ApplicationWindow("org.leon.files", "文件", "Files") {
    directory.path = user_home();
}
void FilesApplication::text_input(const std::string &text) { edit(file_input, text); }
void FilesApplication::on_tick() {
    if (++transfer_ticks % 5 || transfers.empty()) return;
    leon::platform::Client client;
    for (auto i = transfers.begin(); i != transfers.end();) {
        try {
            auto result = client.interaction(i->first);
            if (!(result.value("acknowledged", false) && result.value("focused", false)) &&
                result.value("state", "") != "failed") { ++i; continue; }
            if (result.value("state", "") == "failed") {
                error = result.value("error", "Could not open package");
                app.redraw();
            }
            try { client.revoke_document(i->second); } catch (...) {}
            try { leon::system_request("intents", {{"op", "cancel"}, {"token", i->first}}); } catch (...) {}
        } catch (...) {
            try { client.revoke_document(i->second); } catch (...) {}
        }
        i = transfers.erase(i);
    }
}
void FilesApplication::paint_content(Canvas &c, double bottom) {
    auto t = c.theme;
    double w = c.width;
    c.text(directory.path.string(), {24, 78, w - 48, 40}, 13, t.muted);
    button(c, hits, {20, 126, 72, 44}, "↑", [&] {
        transition_content(-32);
        directory.path = directory.path.parent_path();
        scroll = 0;
    });
    button(c, hits, {100, 126, w - 120, 44}, file_input, [&] { keyboard = true; });
    button(c, hits, {20, 180, (w - 48) / 2, 44}, tr(en, "新建文件", "New file"),
           [&] { perform([&] { directory.create(file_input, false); }); });
    button(c, hits, {28 + (w - 48) / 2, 180, (w - 48) / 2, 44}, tr(en, "新建目录", "New folder"),
           [&] { perform([&] { directory.create(file_input, true); }); });
    auto entries = directory.entries();
    scroll = std::clamp(scroll, 0.0, std::max(0.0, 242 + entries.size() * 60.0 - bottom));
    std::error_code ec;
    double y = 242 - scroll;
    for (auto &entry : entries) {
        if (y >= 230 && y + 54 < bottom) {
            auto p = entry.path();
            bool dir = entry.is_directory(ec);
            button(c, hits, {20, y, w - 94, 48}, (dir ? "▣  " : "·  ") + p.filename().string(),
                   [&, p, dir] {
                       if (dir) {
                           transition_content();
                           directory.path = p;
                           scroll = 0;
                       } else {
                           auto relative = fs::relative(p, user_home()).string();
                           auto extension = p.extension().string();
                           std::transform(extension.begin(), extension.end(), extension.begin(),
                                          [](unsigned char ch) { return char(std::tolower(ch)); });
                           auto mime = extension == ".lmpkg" ? "application/vnd.leonmobile.package" :
                               extension == ".png" ? "image/png" :
                               extension == ".jpg" || extension == ".jpeg" ? "image/jpeg" : "text/plain";
                           leon::platform::Client platform;
                           auto token = platform.offer_document(relative);
                           try {
                               auto interaction = platform.interact({{"action", "open"}, {"mime", mime}, {"document", token}});
                               if (extension == ".lmpkg") transfers.emplace(interaction, token);
                           }
                           catch (...) { platform.revoke_document(token); throw; }
                       }
                   });
            button(c, hits, {w - 66, y, 46, 48}, "×", [&, p] { perform([&] { fs::remove(p); }); });
        }
        y += 60;
    }
}
} // namespace leon::apps
