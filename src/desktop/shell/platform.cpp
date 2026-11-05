#include "shell.hpp"
#include <leon/system.hpp>
using namespace mobile;
namespace leon::shell {
void Shell::platform_refresh() {
    try {
        audio_state = system_request("audio", {{"op", "status"}});
    } catch (...) { audio_state = Json::object(); }
    if (permission.empty()) {
        try {
            auto requests = system_request("documents", {{"op", "pending"}}).at("requests");
            if (!requests.empty()) {
                auto next = requests.front();
                next["file_request"] = true;
                next["document_request"] = true;
                permission = std::move(next);
                if (permission.at("token").dump() != picker_token) {
                    picker_token = permission.at("token").dump(); picker_open("");
                }
            }
        } catch (...) {}
    }
    try {
        auto requests = system_request("intents", {{"op", "pending"}}).at("requests");
        interaction = requests.empty() ? Json::object() : requests.front();
        auto token = interaction.value("token", "");
        if (token != interaction_token) { interaction_token = token; interaction_page = 0; }
        if (!interaction.empty() && page != Page::lock && root_prompt.empty() && permission.empty()) {
            auto phase = interaction.value("state", "");
            if (phase == "delivered") {
                auto token = interaction.at("token");
                auto target = interaction.at("target").get<std::string>();
                system_request("intents", {{"op", "focused"}, {"token", token}});
                interaction = Json::object();
                launch(target);
                interaction = Json::object();
            } else if (phase == "choosing" && interaction.contains("default")) {
                system_request("intents", {{"op", "choose"}, {"token", interaction.at("token")},
                                           {"target", interaction.at("default")}});
            }
        }
    } catch (...) { interaction = Json::object(); }
}
void Shell::paint_interaction(Canvas &c) {
    if (interaction.empty() || page == Page::lock || !root_prompt.empty() || !permission.empty()) return;
    auto t = c.theme;
    double w = c.width, y = std::max(60.0, (c.height - 580) / 2.0);
    hits.clear();
    c.rounded({0, 0, w, double(c.height)}, {0, 0, 0, .45}, 0);
    c.rounded({24, y, w - 48, 580}, t.surface, 28);
    c.text(tr(en, "使用哪个应用？", "Choose an app"), {44, y + 20, w - 88, 44}, 25, t.text, true);
    c.text(interaction.value("source", ""), {44, y + 72, w - 88, 28}, 13, t.muted);
    auto payload = interaction.value("payload", Json::object());
    auto preview = payload.value("uri", payload.value("mime", ""));
    c.text(preview, {44, y + 106, w - 88, 50}, 15, t.muted);
    auto state = interaction.value("state", "");
    if (state == "choosing") {
        auto candidates = interaction.value("candidates", Json::array());
        double top = y + 168;
        for (size_t i = interaction_page * 5; i < std::min<size_t>((interaction_page + 1) * 5, candidates.size()); ++i) {
            auto id = candidates[i].at("id").get<std::string>();
            auto label = candidates[i].value(en ? "name_en" : "name", id);
            if (label.empty()) label = id;
            auto token = interaction.at("token");
            button(c, hits, {44, top, w - 158, 48}, label, [this, token, id] {
                system_request("intents", {{"op", "choose"}, {"token", token}, {"target", id}});
                system_refresh();
            });
            button(c, hits, {w - 106, top, 62, 48}, tr(en, "始终", "Always"), [this, token, id] {
                system_request("intents", {{"op", "choose"}, {"token", token}, {"target", id}, {"always", true}});
                system_refresh();
            });
            top += 58;
        }
        button(c, hits, {44, y + 458, 64, 34}, "‹", [this] { if (interaction_page) --interaction_page; });
        button(c, hits, {w - 108, y + 458, 64, 34}, "›", [this, candidates] {
            if ((interaction_page + 1) * 5 < candidates.size()) ++interaction_page;
        });
    } else if (state == "failed") {
        c.text(tr(en, "无法打开应用", "Couldn't open app"), {44, y + 180, w - 88, 60}, 19, t.text);
        c.text(tr(en, "请取消后重试，或在设置中检查应用状态。",
                  "Cancel and try again, or check the app in Settings."),
               {44, y + 248, w - 88, 96}, 14, t.muted);
    } else
        c.text(tr(en, "正在打开应用…", "Opening app…"), {44, y + 180, w - 88, 60}, 19, t.text);
    button(c, hits, {44, y + 500, w - 88, 48}, tr(en, "取消", "Cancel"), [this] {
        system_request("intents", {{"op", "cancel"}, {"token", interaction.at("token")}});
        interaction = Json::object(); system_refresh();
    });
}
void Shell::paint_media(Canvas &c, double y) {
    if (!audio_state.contains("current")) return;
    auto current = audio_state.at("current");
    auto t = c.theme; double w = c.width;
    c.rounded({24, y, w - 48, 116}, t.surface, 26);
    // Credential-protected lock screens expose controls without media metadata.
    auto title = page == Page::lock && authentication.configured
        ? tr(en, "媒体播放", "Media playback") : current.value("title", "");
    c.text(title, {44, y + 12, w - 164, 40}, 19, t.text, true);
    bool paused = current.value("paused", false);
    c.text(paused ? tr(en, "已暂停", "Paused") : tr(en, "正在播放", "Playing"),
           {44, y + 58, w - 164, 30}, 14, t.muted);
    button(c, hits, {w - 118, y + 26, 70, 58}, paused ? "▶" : "Ⅱ", [this, current] {
        system_request("audio", {{"op", "toggle"}, {"token", current.at("token")}});
        platform_refresh();app.redraw();
    });
}
} // namespace leon::shell
