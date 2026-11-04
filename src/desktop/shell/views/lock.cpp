#include "../shell.hpp"
#include <cstring>
using namespace mobile;
namespace leon::shell {
void Shell::paint_lock(Canvas &c, const std::string &time) {
    auto t = c.theme;
    double w = c.width, h = c.height;
    if (!unlock_entry) {
        double offset = c.animate("shell/lock/drag", pressed && !c.reduced_motion() ? lock_drag : 0,
                                  pressed ? MotionSpec{0} : motion::spatial);
        c.save();
        cairo_translate(c.cr, 0, -offset);
        c.circle(w * .75, h * .45, w * .6, t.container);
        c.text(time, {24, h * .22, w - 48, 130}, 92, t.primary, false, 1);
        c.text("LeonMobile", {24, h * .22 + 140, w - 48, 50}, 24, t.text, true, 1);
        c.text(authentication.ok ? tr(en, "向上滑动解锁", "Swipe up to unlock")
                                 : tr(en, "认证服务正在恢复", "Authentication service recovering"),
               {24, h - 180, w - 48, 48}, 16, t.muted, false, 1);
        c.restore();
        return;
    }
    bool pin = !strcmp(authentication.kind, "pin");
    c.text(pin ? tr(en, "输入设备 PIN", "Enter device PIN")
               : tr(en, "输入设备密码", "Enter device password"),
           {24, 130, w - 48, 56}, 29, t.text, true, 1);
    c.text(tr(en, "设备凭据与 Root 密码相互独立", "Device credential is separate from root"),
           {24, 198, w - 48, 32}, 13, t.muted, false, 1);
    std::string masked;
    for (size_t i = 0; i < device_secret.size(); ++i)
        if ((static_cast<unsigned char>(device_secret[i]) & 0xc0) != 0x80)
            masked += "•";
    c.rounded({40, 252, w - 80, 68}, t.container, 24);
    c.text(masked.empty() ? (pin ? "PIN" : tr(en, "密码", "Password")) : masked,
           {56, 260, w - 112, 52}, 25, t.text, false, 1);
    if (!pin)
        click(hits, {40, 252, w - 80, 68}, [this] { app.text_input(true, true); });
    auto message = authentication.retry_after_ms > 0
                       ? tr(en, "请稍后重试 · ", "Try again in ") +
                             std::to_string((authentication.retry_after_ms + 999) / 1000) +
                             tr(en, " 秒", " seconds")
                       : auth_message;
    c.text(message, {24, 330, w - 48, 58}, 14, t.muted, false, 1);
    if (pin) {
        for (int n = 1; n <= 9; ++n) {
            double x = 56 + ((n - 1) % 3) * (w - 112) / 3;
            double y = 406 + ((n - 1) / 3) * 82;
            button(c, hits, {x, y, (w - 136) / 3, 66}, std::to_string(n),
                   [this, n] { unlock_text(std::to_string(n)); });
        }
        button(c, hits, {56, 652, (w - 136) / 3, 66}, "⌫", [this] { unlock_text("\b"); });
        button(c, hits, {56 + (w - 112) / 3, 652, (w - 136) / 3, 66}, "0",
               [this] { unlock_text("0"); });
        button(
            c, hits, {56 + 2 * (w - 112) / 3, 652, (w - 136) / 3, 66}, "→",
            [this] { unlock_submit(); }, true);
    } else {
        button(
            c, hits, {40, 398, w - 80, 52}, tr(en, "解锁", "Unlock"), [this] { unlock_submit(); },
            true);
    }
    button(c, hits, {40, pin ? 748.0 : 464.0, w - 80, 48}, tr(en, "返回", "Back"), [this] {
        clear_device_secret();
        unlock_entry = false;
    });
}
} // namespace leon::shell
