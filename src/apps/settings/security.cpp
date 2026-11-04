#include "application.hpp"
#include "request.hpp"
#include <cstring>
#include <sys/prctl.h>
#include <sys/resource.h>
using namespace mobile;
namespace leon::apps {
void SettingsApplication::clear_device_fields() {
    for (auto *s : {&device_current_, &device_new_, &device_confirm_})
        erase_secret(*s);
    security_dialog_ = false;
    keyboard = false;
    app.text_input(false, true);
}
void SettingsApplication::security_refresh() {
    if (security_job_.valid() &&
        security_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = security_job_.get();
        clear_device_fields();
        security_message_ = result.ok ? tr(en, "屏幕锁定已更新", "Screen lock updated")
                                      : tr(en, "验证或保存失败，请稍后重试",
                                           "Authentication or save failed; retry shortly");
        security_status_ = result;
        app.redraw();
    }
    if (!security_job_.valid()) {
        auto state = device_request({{"op", "status"}}, 150);
        if (state.generation != security_status_.generation ||
            state.retry_after_ms != security_status_.retry_after_ms ||
            state.ok != security_status_.ok || state.locked != security_status_.locked)
            app.redraw();
        if ((!state.ok || state.locked) && security_dialog_)
            clear_device_fields();
        security_status_ = state;
    }
}
void SettingsApplication::security_begin(const std::string &kind) {
    if (security_job_.valid() || !security_status_.ok || security_status_.locked)
        return;
    clear_device_fields();
    struct rlimit core {
        0, 0
    };
    setrlimit(RLIMIT_CORE, &core);
    prctl(PR_SET_DUMPABLE, 0);
    for (auto *s : {&device_current_, &device_new_, &device_confirm_})
        s->reserve(128);
    security_kind_ = kind;
    security_dialog_ = true;
    security_field_ = security_status_.configured ? 0 : 1;
    security_message_.clear();
    keyboard = true;
    scroll = 0;
    app.redraw();
}
void SettingsApplication::security_submit() {
    if (!security_dialog_ || security_job_.valid() || !security_status_.ok ||
        security_status_.retry_after_ms)
        return;
    if (security_kind_ != "none") {
        bool pin = security_kind_ == "pin";
        bool valid = device_new_.size() >= (pin ? 6 : 8) && device_new_.size() <= (pin ? 12 : 128);
        if (pin)
            for (char c : device_new_)
                valid &= c >= '0' && c <= '9';
        if (!valid || device_new_ != device_confirm_) {
            security_message_ = tr(en, "两次输入须一致：PIN 为 6–12 位数字，密码为 8–128 字节",
                                   "Entries must match: PIN 6–12 digits; password 8–128 bytes");
            app.redraw();
            return;
        }
    }
    Json operation = {{"op", "configure"},
                      {"kind", security_kind_},
                      {"secret", device_current_},
                      {"new_secret", device_new_}};
    clear_device_fields();
    security_message_ = tr(en, "正在验证并保存…", "Verifying and saving…");
    security_job_ = std::async(std::launch::async, [operation = std::move(operation)]() mutable {
        return device_request(std::move(operation), 10000);
    });
    scroll = 0;
    app.redraw();
}
void SettingsApplication::security_text(const std::string &text) {
    if (!security_dialog_ || !keyboard || security_job_.valid())
        return;
    if (text == "\n" || text == "\r") {
        if (security_kind_ != "none" && security_field_ < 2) {
            ++security_field_;
            scroll = security_field_ == 2 ? 96 : 0;
        } else
            security_submit();
        app.redraw();
        return;
    }
    std::string &field = security_field_ == 0   ? device_current_
                         : security_field_ == 1 ? device_new_
                                                : device_confirm_;
    if (text == "\b") {
        // Erase removed bytes as well as the active string at cancellation.
        size_t before = field.size();
        backspace(field);
        if (before > field.size())
            root_clear(field.data() + field.size(), before - field.size());
    } else {
        bool pin =
            security_field_ == 0 ? !strcmp(security_status_.kind, "pin") : security_kind_ == "pin";
        for (unsigned char c : text)
            if (c < 32 || c == 127 || (pin && (c < '0' || c > '9')))
                return;
        if (field.size() + text.size() <= (pin ? 12 : 128))
            field += text;
    }
    app.redraw();
}
void SettingsApplication::paint_security(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    auto action = [&](Rect r, const std::string &label, std::function<void()> callback,
                      bool active = false) {
        c.rounded(r, active ? t.primary : t.container, 22);
        c.text(label, r, 16, active ? t.on_primary : t.primary, true, 1);
        content_hit(r, [callback = std::move(callback)](double, double) { callback(); });
    };
    if (security_dialog_) {
        c.text(security_kind_ == "none"  ? tr(en, "关闭屏幕锁定", "Turn off screen lock")
               : security_kind_ == "pin" ? tr(en, "设置设备 PIN", "Set device PIN")
                                         : tr(en, "设置设备密码", "Set device password"),
               {24, 164, w - 48, 44}, 24, t.text, true);
        c.text(tr(en, "与 Root 密码独立；不会显示或保存明文。",
                  "Separate from root. Input stays private."),
               {24, 212, w - 48, 42}, 13, t.muted);
        double y = 266;
        auto field = [&](const std::string &label, const std::string &secret, int index) {
            c.text(label, {28, y, w - 56, 26}, 13, t.muted);
            c.rounded({20, y + 30, w - 40, 48}, security_field_ == index ? t.container : t.surface,
                      14);
            std::string masked;
            for (unsigned char value : secret)
                if ((value & 0xc0) != 0x80)
                    masked += "•";
            c.text(masked.empty() ? tr(en, "点击输入", "Tap to enter") : masked,
                   {34, y + 30, w - 68, 48}, 18, t.text);
            content_hit({20, y + 30, w - 40, 48}, [this, index](double, double) {
                security_field_ = index;
                keyboard = true;
            });
            y += 94;
        };
        if (security_status_.configured)
            field(tr(en, "当前设备凭据", "Current device credential"), device_current_, 0);
        if (security_kind_ != "none") {
            field(security_kind_ == "pin"
                      ? tr(en, "新 PIN（6–12 位数字）", "New PIN (6–12 digits)")
                      : tr(en, "新密码（8–128 字节）", "New password (8–128 bytes)"),
                  device_new_, 1);
            field(tr(en, "再次输入", "Enter again"), device_confirm_, 2);
        }
        action({20, y + 6, (w - 48) / 2, 48}, tr(en, "取消", "Cancel"),
               [this] { clear_device_fields(); });
        action(
            {28 + (w - 48) / 2, y + 6, (w - 48) / 2, 48}, tr(en, "确认", "Confirm"),
            [this] { security_submit(); }, true);
        c.text(security_message_, {24, y + 66, w - 48, 64}, 13, t.muted);
        return;
    }
    c.rounded({20, 164, w - 40, 112}, t.container, 26);
    c.text(tr(en, "屏幕锁定", "Screen lock"), {36, 180, w - 72, 36}, 23, t.text, true);
    std::string mode = !security_status_.ok ? tr(en, "认证服务不可用", "Authentication unavailable")
                       : !security_status_.configured
                           ? tr(en, "未设置 · 滑动解锁", "None · swipe to unlock")
                       : !strcmp(security_status_.kind, "pin")
                           ? tr(en, "设备 PIN 已启用", "Device PIN enabled")
                           : tr(en, "设备密码已启用", "Device password enabled");
    c.text(mode, {36, 226, w - 72, 30}, 15, t.muted);
    action({20, 300, w - 40, 56}, tr(en, "设置或更换 PIN", "Set or change PIN"),
           [this] { security_begin("pin"); });
    action({20, 372, w - 40, 56}, tr(en, "设置或更换密码", "Set or change password"),
           [this] { security_begin("password"); });
    if (security_status_.configured)
        action({20, 444, w - 40, 56}, tr(en, "关闭屏幕锁定", "Turn off screen lock"),
               [this] { security_begin("none"); });
    c.text(tr(en, "开机、手动锁定与空闲锁定后需要验证。",
              "Authenticate after boot, manual and idle locking."),
           {28, 516, w - 56, 56}, 14, t.muted);
    c.text(security_status_.retry_after_ms > 0
               ? tr(en, "请等待后重试：", "Retry in ") +
                     std::to_string((security_status_.retry_after_ms + 999) / 1000) + "s"
               : security_message_,
           {28, 580, w - 56, 70}, 13, t.muted);
}
} // namespace leon::apps
