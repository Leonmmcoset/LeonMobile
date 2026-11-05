#include "control.hpp"
#include "request.hpp"
#include "shell.hpp"
#include "system_request.hpp"
#include <sys/prctl.h>
#include <sys/resource.h>
using namespace mobile;
namespace leon::shell {
void Shell::root_clear_prompt() {
    erase_secret(root_secret);
    root_prompt = Json::object();
    root_credentials = false;
    root_detail_page = 0;
    root_message.clear();
    app.text_input(false, true);
}
void Shell::root_refresh() {
    if (root_job.valid() &&
        root_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = root_job.get();
        if (root_prompt.value("token", "") == root_job_token) {
            if (result.value("ok", false))
                root_clear_prompt();
            else
                root_message = result.value("error", "Root request failed");
        }
        root_job_token.clear();
        app.redraw();
    }
    if (page == Page::lock || !authentication.ok || authentication.locked) {
        if (!root_prompt.empty())
            root_clear_prompt();
        return;
    }
    auto reply = root_control({{"op", "app_pending"}}, 350);
    if (!reply.value("ok", false)) {
        // A service failure cannot leave a stale password entry accepting input.
        if (!root_prompt.empty())
            root_clear_prompt();
        return;
    }
    auto requests = reply.value("requests", Json::array());
    Json next = Json::object();
    for (const auto &item : requests)
        if (item.value("token", "") == root_prompt.value("token", "!"))
            next = item;
    if (next.empty() && !requests.empty())
        next = requests.front();
    if (next.value("token", "") != root_prompt.value("token", "")) {
        root_clear_prompt();
        root_prompt = std::move(next);
        if (!root_prompt.empty()) {
            root_secret.reserve(128);
            struct rlimit core = {0, 0};
            setrlimit(RLIMIT_CORE, &core);
            prctl(PR_SET_DUMPABLE, 0);
            reset_shade();
            touch_feedback.cancel();
        }
        app.redraw();
    }
}
void Shell::root_submit(bool allow, bool block) {
    if (root_prompt.empty() || root_job.valid() ||
        (allow && (!root_credentials || root_secret.empty())))
        return;
    Json request;
    request["op"] = "app_decide";
    request["token"] = root_prompt.at("token");
    request["allow"] = allow;
    request["block"] = block;
    if (allow)
        request["password"] = root_secret;
    erase_secret(root_secret);
    app.text_input(false, true);
    root_message = tr(en, "正在验证…", "Verifying…");
    root_job_token = root_prompt.at("token");
    root_job = std::async(std::launch::async, [request = std::move(request)]() mutable {
        return root_control(std::move(request), 10000);
    });
    app.redraw();
}
void Shell::root_cancel() {
    if (root_prompt.empty())
        return;
    auto token = root_prompt.at("token");
    erase_secret(root_secret);
    app.text_input(false, true);
    // Cancellation also wins if the password worker just approved this token.
    root_control({{"op", "app_abort"}, {"token", token}}, 2200);
    root_clear_prompt();
    app.redraw();
}
void Shell::root_text(const std::string &text) {
    if (root_prompt.empty() || !root_credentials || root_job.valid() || !app.focused())
        return;
    if (text == "\n" || text == "\r") {
        root_submit(true);
        return;
    }
    if (text == "\b") {
        size_t before = root_secret.size();
        backspace(root_secret);
        root_clear(root_secret.data() + root_secret.size(), before - root_secret.size());
    } else if (root_secret.size() + text.size() <= 128 &&
               std::none_of(text.begin(), text.end(),
                            [](unsigned char c) { return c < 32 || c == 127; }))
        root_secret += text;
    app.redraw();
}
std::vector<std::string> Shell::root_command_lines() const {
    // ASCII JSON encoding makes controls, bidi, quotes and argument boundaries explicit.
    auto text = "helper: " + Json(root_prompt.value("entry", "")).dump(-1, ' ', true) +
                "\nargs: " + root_prompt.value("args", Json::array()).dump(-1, ' ', true);
    size_t columns = std::clamp((app.width() - 64) / 12, 8, 32);
    std::vector<std::string> lines;
    for (size_t at = 0; at < text.size();) {
        size_t end = std::min(at + columns, text.size());
        auto newline = text.find('\n', at);
        if (newline < end)
            end = newline;
        lines.push_back(text.substr(at, end - at));
        at = end + (end < text.size() && text[end] == '\n' ? 1 : 0);
    }
    return lines;
}
void Shell::paint_root(Canvas &c) {
    if (root_prompt.empty() || page == Page::lock)
        return;
    auto t = c.theme;
    double w = c.width, h = c.height;
    hits.clear();
    c.rounded({0, 42, w, h - 42}, t.background, 0);
    c.text(tr(en, "系统 · Root 授权", "System · Root authorization"), {24, 62, w - 48, 46}, 26,
           t.text, true);
    c.text(tr(en, "此 helper 可修改系统并读取所有数据。仅批准你信任的应用。",
              "This helper can modify the system and read all data. Approve only apps you trust."),
           {24, 116, w - 48, 68}, 16, t.primary);
    c.text(root_prompt.value("id", ""), {24, 194, w - 48, 62}, 16, t.text, true);
    c.text(tr(en, "本地未签名应用 · 版本 ", "Local unsigned app · version ") +
               root_prompt.value("version", ""),
           {24, 260, w - 48, 30}, 13, t.muted);
    bool busy = root_job.valid();
    if (root_credentials) {
        c.text(tr(en, "Root 密码（与锁屏密码独立）", "Root password (separate from screen lock)"),
               {24, 302, w - 48, 34}, 15, t.text);
        c.rounded({24, 344, w - 48, 56}, t.container, 20);
        std::string masked;
        for (unsigned char byte : root_secret)
            if ((byte & 0xc0) != 0x80)
                masked += "•";
        c.text(masked.empty() ? tr(en, "输入 Root 密码", "Enter Root password") : masked,
               {40, 348, w - 80, 48}, 20, t.text);
        click(hits, {24, 344, w - 48, 56}, [this] {
            if (!root_job.valid())
                app.text_input(true, true);
        });
        button(c, hits, {24, 420, (w - 56) / 2, 52}, tr(en, "取消", "Cancel"),
               [this] { root_cancel(); });
        if (!busy)
            button(
                c, hits, {32 + (w - 56) / 2, 420, (w - 56) / 2, 52},
                tr(en, "仅允许本次", "Allow once"), [this] { root_submit(true); }, true);
        c.text(root_message, {24, 484, w - 48, 64}, 13, t.muted);
        if (!busy)
            button(c, hits, {24, 558, w - 48, 44},
                   tr(en, "返回查看 helper 与参数", "Review helper and arguments"), [this] {
                       erase_secret(root_secret);
                       root_credentials = false;
                       app.text_input(false, true);
                   });
        return;
    }
    c.text(tr(en, "核对将以 UID 0 执行的文件与完整参数",
              "Review the helper and all arguments for UID 0"),
           {24, 304, w - 48, 42}, 15, t.text);
    auto lines = root_command_lines();
    size_t pages = std::max<size_t>(1, (lines.size() + 7) / 8);
    root_detail_page = std::min(root_detail_page, pages - 1);
    c.rounded({20, 360, w - 40, 214}, t.container, 20);
    for (size_t i = root_detail_page * 8; i < lines.size() && i < (root_detail_page + 1) * 8; ++i)
        c.text(lines[i], {32, 374 + (i % 8) * 23.0, w - 64, 23}, 12, t.text);
    c.text(std::to_string(root_detail_page + 1) + " / " + std::to_string(pages),
           {w / 2 - 40, 590, 80, 40}, 14, t.muted, false, 1);
    if (root_detail_page)
        button(c, hits, {24, 586, 120, 44}, tr(en, "上一页", "Previous"),
               [this] { --root_detail_page; });
    if (root_detail_page + 1 < pages)
        button(c, hits, {w - 144, 586, 120, 44}, tr(en, "下一页", "Next"),
               [this] { ++root_detail_page; });
    button(c, hits, {24, 654, w - 48, 52}, tr(en, "拒绝本次", "Deny once"),
           [this] { root_submit(false); });
    if (!busy && root_detail_page + 1 == pages)
        button(
            c, hits, {24, 722, w - 48, 52},
            tr(en, "继续 · 输入 Root 密码", "Continue · enter Root password"),
            [this] {
                root_credentials = true;
                root_message.clear();
                app.text_input(true, true);
            },
            true);
    button(c, hits, {24, 790, w - 48, 48},
           tr(en, "拒绝并禁止此应用再次申请", "Deny and block this app"),
           [this] { root_submit(false, true); });
    c.text(root_message, {24, 850, w - 48, 50}, 13, t.muted);
}
} // namespace leon::shell
