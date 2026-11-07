#include "application.hpp"
#include <algorithm>
using namespace mobile;
namespace leon::apps {
namespace {
std::string display(std::string text) {
    for (char &c : text) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    return text;
}
}
void InstallerApplication::paint_content(Canvas &c, double bottom) {
    auto t = c.theme;
    double w = c.width;
    auto state = transaction.value("state", "idle");
    bool busy = job.valid() || !activity.empty() || state == "checking" || state == "installing";
    if (!activity.empty() || state == "checking" || state == "installing") {
        c.rounded({24, 102, w - 48, 210}, t.container, 30);
        c.text("↓", {40, 118, w - 80, 80}, 52, t.primary, true, 1);
        auto message = activity == "choosing" ? tr(en, "请在系统窗口选择安装包", "Choose a package in the system picker")
                     : activity == "reading" ? tr(en, "正在读取安装包…", "Reading package…")
                     : state == "installing" ? tr(en, "正在安装…", "Installing…")
                     : tr(en, "正在检查安装包…", "Checking package…");
        c.text(message, {40, 220, w - 80, 60}, 19, t.text, true, 1);
        c.text(tr(en, "安装过程中可以返回桌面，稍后查看结果。", "You can return home and check the result later."),
               {32, 340, w - 64, 90}, 15, t.muted);
        return;
    }
    if (state == "ready" || state == "installed") {
        const auto &review = transaction.at("review");
        const auto &package = review.at("package");
        bool done = state == "installed";
        bool allowed = review.at("installable");
        c.save();
        c.clip({0, 80, w, bottom - 164});
        double y = 96 - scroll;
        c.rounded({24, y, w - 48, 138}, t.container, 28);
        c.text(display(en ? package.value("name_en", package.at("name").get<std::string>())
                          : package.at("name").get<std::string>()),
               {40, y + 10, w - 80, 42}, 25, t.text, true);
        c.text(package.at("id"), {40, y + 56, w - 80, 30}, 13, t.muted);
        std::string version = package.at("version");
        if (!review.at("installed").is_null())
            version = review["installed"]["version"].get<std::string>() + " → " + version;
        c.text(version + "  ·  AArch64", {40, y + 90, w - 80, 30}, 16, t.primary, true);
        y += 158;
        c.text(done ? tr(en, "安装成功", "Installed successfully") : tr(en, "来源 · 本地文件", "Source · Local file"),
               {32, y, w - 64, 32}, 18, t.primary, true);
        c.text(done ? tr(en, "请从桌面打开应用；已运行的旧版本需重新打开。", "Open from the home screen; restart any running old version.")
                    : display(source.empty() ? tr(en, "已恢复的待确认安装包", "Restored package review") : source),
               {32, y + 36, w - 64, 52}, 14, t.muted);
        y += 104;
        c.rounded({24, y, w - 48, 98}, t.surface, 22);
        c.text(tr(en, "未签名 · 发布者身份未经验证", "Unsigned · Publisher identity unverified"),
               {40, y + 8, w - 80, 34}, 16, t.text, true);
        c.text(tr(en, "完整性校验已通过。请确认你信任这个文件的来源。", "Integrity checks passed. Confirm that you trust this file's source."),
               {40, y + 46, w - 80, 44}, 13, t.muted);
        y += 122;
        c.text(tr(en, "声明的权限", "Declared permissions"), {32, y, w - 64, 34}, 19, t.text, true);
        y += 42;
        auto permissions = package.at("permissions");
        if (permissions.empty()) {
            c.text(tr(en, "未声明额外权限", "No additional permissions declared"), {32, y, w - 64, 42}, 15, t.muted);
            y += 50;
        }
        for (const auto &item : permissions) {
            std::string permission = item;
            std::string label = permission == "network" ? tr(en, "网络访问", "Network access")
                              : permission == "files.import" ? tr(en, "选择文件", "Choose files")
                              : tr(en, "请求运行 Root 辅助程序", "Request a Root helper");
            auto added = review.at("added_permissions");
            if (!review.at("installed").is_null() && std::find(added.begin(), added.end(), item) != added.end())
                label += tr(en, " · 新增", " · New");
            c.text(label, {32, y, w - 64, 40}, 16, t.text);
            y += 46;
        }
        if (package.contains("root_description")) {
            c.text(tr(en, "应用声明的 Root 用途：", "App-declared Root purpose: ") +
                       display(package.at("root_description")), {32, y, w - 64, 80}, 14, t.muted);
            y += 88;
        }
        c.text(tr(en, "安装不会自动授权。使用时仍需单独确认。", "Installation grants no permissions. Approve requests separately."),
               {32, y, w - 64, 60}, 14, t.muted);
        y += 72;
        c.text("SHA-256", {32, y, w - 64, 30}, 13, t.primary, true);
        c.text(review.at("sha256"), {32, y + 32, w - 64, 64}, 12, t.muted);
        y += 112;
        if (!allowed && !done) {
            c.text(review.value("reason", ""), {32, y, w - 64, 64}, 15, t.primary, true);
            y += 72;
        }
        c.restore();
        scroll = std::clamp(scroll, 0.0, std::max(0.0, y + scroll - (bottom - 88)));
        if (done || !allowed) {
            button(c, hits, {24, bottom - 68, w - 48, 48}, tr(en, "完成", "Done"), [this] { cancel(); }, true);
        } else if (!busy) {
            button(c, hits, {24, bottom - 68, (w - 60) / 2, 48}, tr(en, "取消", "Cancel"), [this] { cancel(); });
            button(c, hits, {36 + (w - 60) / 2, bottom - 68, (w - 60) / 2, 48},
                   review.at("installed").is_null() ? tr(en, "安装", "Install") : tr(en, "更新", "Update"),
                   [this] { request({{"op", "package_commit"}, {"token", transaction.at("token")}}); }, true);
        }
        return;
    }
    c.rounded({24, 102, w - 48, 214}, t.container, 30);
    c.text(state == "failed" ? "!" : "↓", {40, 114, w - 80, 88}, 52, t.primary, true, 1);
    c.text(state == "failed" ? tr(en, "无法安装应用", "Could not install app") : tr(en, "安装本地应用", "Install a local app"),
           {40, 220, w - 80, 60}, 23, t.text, true, 1);
    c.text(state == "failed" ? transaction.value("error", "")
                             : tr(en, "选择 .lmpkg 文件，检查应用信息后确认安装。", "Choose a .lmpkg file, review its details and confirm installation."),
           {32, 342, w - 64, 120}, 16, t.muted);
    if (!busy)
        button(c, hits, {24, 492, w - 48, 52}, tr(en, "选择安装包", "Choose package"), [this] { choose(); }, true);
    c.text(tr(en, "也可以在“文件”中点击安装包。", "You can also open a package from Files."),
           {32, 570, w - 64, 74}, 14, t.muted);
}
} // namespace leon::apps
