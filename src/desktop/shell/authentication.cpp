#include "control.hpp"
#include "request.hpp"
#include "shell.hpp"
#include <cstring>
using namespace mobile;
namespace leon::shell {
void Shell::clear_device_secret() {
    erase_secret(device_secret);
    app.text_input(false, true);
}
void Shell::authentication_refresh() {
    auto next = device_request({{"op", "status"}}, 150);
    bool changed = std::string(next.epoch) != authentication.epoch;
    authentication = next;
    if (!next.ok || next.locked) {
        if (page != Page::lock || changed) {
            notifications=Json::array();notifications_lock_epoch.clear();
            root_clear_prompt();
            clear_device_secret();
            unlock_entry = unlock_pending = false;
            ready_epoch.clear();
            reset_shade();
            page = Page::lock;
            active_id.clear();
            background_pending = true;
            synchronize_background();
            app.redraw();
        }
    } else if (page == Page::lock && unlock_pending) {
        unlock_pending = false;
        clear_device_secret();
        unlock_entry = false;
        show_page(Page::home);
    }
    if (auth_job.valid() &&
        auth_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = auth_job.get();
        if (result.ok && page == Page::lock && result.locked &&
            !strcmp(result.epoch, authentication.epoch)) {
            unlock_pending = control("unlock " + std::string(result.token));
            auth_message = unlock_pending
                               ? tr(en, "正在解锁…", "Unlocking…")
                               : tr(en, "无法连接桌面服务", "Desktop service unavailable");
        } else {
            unlock_pending = false;
            auth_message = tr(en, "认证失败，请检查密码并稍后重试",
                              "Authentication failed; check your credential and retry");
        }
        root_clear(result.token, sizeof(result.token));
        app.redraw();
    }
    if (!app.focused() && !device_secret.empty())
        clear_device_secret();
    bool keyboard = page == Page::lock && unlock_entry && authentication.configured &&
                    !strcmp(authentication.kind, "password") && !auth_job.valid() &&
                    !unlock_pending;
    if (!app.focused() && !root_secret.empty()) {
        erase_secret(root_secret);
        root_credentials = false;
    }
    bool root_keyboard = page != Page::lock && !root_prompt.empty() && root_credentials &&
                         !root_job.valid() && app.focused();
    app.text_input(keyboard || root_keyboard, true);
    if (page == Page::lock)
        app.redraw();
}
void Shell::unlock_start() {
    if (!authentication.ok || auth_job.valid() || unlock_pending)
        return;
    if (authentication.configured && !unlock_entry) {
        unlock_entry = true;
        auth_message.clear();
        app.redraw();
        return;
    }
    unlock_submit();
}
void Shell::unlock_submit() {
    if (authentication.retry_after_ms) {
        clear_device_secret();
        app.redraw();
        return;
    }
    if (!authentication.ok || auth_job.valid() || unlock_pending)
        return;
    Json request = {{"op", "authenticate"}, {"purpose", "unlock"}, {"secret", device_secret}};
    clear_device_secret();
    auth_message = tr(en, "正在验证…", "Verifying…");
    auth_job = std::async(std::launch::async, [request = std::move(request)]() mutable {
        return device_request(std::move(request), 10000);
    });
    app.redraw();
}
void Shell::unlock_text(const std::string &text) {
    if (page != Page::lock || !unlock_entry || authentication.retry_after_ms || auth_job.valid() ||
        unlock_pending)
        return;
    if (text == "\n" || text == "\r") {
        unlock_submit();
        return;
    }
    if (text == "\b") {
        size_t before = device_secret.size();
        backspace(device_secret);
        root_clear(device_secret.data() + device_secret.size(), before - device_secret.size());
    } else {
        bool pin = !strcmp(authentication.kind, "pin");
        for (unsigned char c : text)
            if (c < 32 || c == 127 || (pin && (c < '0' || c > '9')))
                return;
        if (device_secret.size() + text.size() <= (pin ? 12 : 128))
            device_secret += text;
    }
    app.redraw();
}
} // namespace leon::shell
