#include "application.hpp"
#include <unistd.h>
void RootDemo::paint(lmui::Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    c.clear(t.background);
    c.text("Root 权限示例", {24, 36, w - 48, 62}, 32, t.text, true);
    c.text("已安装的第三方 ELF · LMUI", {28, 106, w - 56, 32}, 15, t.muted);
    c.rounded({20, 164, w - 40, 116}, t.container, 24);
    c.text("应用主进程 UID: " + std::to_string(getuid()), {36, 180, w - 72, 38}, 22, t.text, true);
    c.text("授权后仍保持应用身份；Root 仅在独立 helper 中执行。", {36, 222, w - 72, 48}, 14,
           t.muted);
    c.text("此示例只读取进程身份和内核信息。每次运行都会请求系统确认与 Root 密码。",
           {24, 300, w - 48, 82}, 17, t.text);
    actions = lmui::Column({lmui::Button(job.valid() ? "取消请求" : "请求单次 Root 诊断", [this] {
        if (job.valid())
            stop.request_stop();
        else
            request();
    })});
    actions.layout({24, 402, w - 48, 60});
    actions.draw(c);
    std::string status =
        !attempted ? "尚未申请 Root"
        : job.valid()
            ? phase == leon::RootPhase::pending ? "等待系统授权…" : "独立 Root helper 正在执行…"
        : result.cancelled ? "请求已取消"
        : result.ok()      ? "诊断完成 · helper 退出码 " + std::to_string(result.status)
                           : "请求未获批准";
    c.text(status, {24, 482, w - 48, 52}, 18, t.primary, true);
    c.rounded({20, 552, w - 40, 260}, t.surface, 24);
    c.text(result.error.empty() ? result.output.empty() ? "诊断输出将显示在这里" : result.output
                                : result.error,
           {36, 566, w - 72, 230}, 16, t.text);
    c.text("这是一份开发示例，没有永久 Root 授权。", {24, 832, w - 48, 52}, 13, t.muted);
    touch.draw(c);
}
