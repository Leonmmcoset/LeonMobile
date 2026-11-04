#include "backend.hpp"
namespace lmui {
void Application::Impl::text_update() {
    if (!text_input || !text_entered ||
        (text_enabled == text_wanted && (!text_wanted || text_private_sent == text_private)))
        return;
    text_enabled = text_wanted;
    if (text_enabled) {
        zwp_text_input_v3_enable(text_input);
        zwp_text_input_v3_set_content_type(text_input,
                                           text_private
                                               ? ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT |
                                                     ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA
                                               : 0,
                                           text_private ? ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD
                                                        : ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    } else
        zwp_text_input_v3_disable(text_input);
    zwp_text_input_v3_commit(text_input);
    text_private_sent = text_private;
    ++text_serial;
}
void Application::Impl::text_open() {
    if (!text_manager || !seat)
        return;
    text_input = zwp_text_input_manager_v3_get_text_input(text_manager, seat);
    static const zwp_text_input_v3_listener events = {
        [](void *data, zwp_text_input_v3 *, wl_surface *) {
            auto &s = *static_cast<Impl *>(data);
            s.text_entered = true;
            s.text_enabled = false;
            s.text_update();
        },
        [](void *data, zwp_text_input_v3 *, wl_surface *) {
            auto &s = *static_cast<Impl *>(data);
            s.text_entered = s.text_enabled = false;
            s.got_text = s.got_preedit = s.got_delete = false;
            if (s.text_preedit)
                s.text_preedit("");
        },
        [](void *data, zwp_text_input_v3 *, const char *text, int32_t, int32_t) {
            auto &s = *static_cast<Impl *>(data);
            s.pending_preedit = text ? text : "";
            s.got_preedit = true;
        },
        [](void *data, zwp_text_input_v3 *, const char *text) {
            auto &s = *static_cast<Impl *>(data);
            s.pending_text = text ? text : "";
            s.got_text = true;
        },
        [](void *data, zwp_text_input_v3 *, uint32_t before, uint32_t after) {
            auto &s = *static_cast<Impl *>(data);
            s.delete_before = before;
            s.delete_after = after;
            s.got_delete = true;
        },
        [](void *data, zwp_text_input_v3 *, uint32_t serial) {
            auto &s = *static_cast<Impl *>(data);
            if (s.text_entered && s.text_wanted && serial == s.text_serial) {
                if (s.got_delete && s.text_delete)
                    s.text_delete(s.delete_before, s.delete_after);
                if (s.got_text && s.text_commit)
                    s.text_commit(s.pending_text);
                if (s.got_preedit && s.text_preedit)
                    s.text_preedit(s.pending_preedit);
                s.dirty = true;
            }
            s.got_text = s.got_preedit = s.got_delete = false;
        }};
    zwp_text_input_v3_add_listener(text_input, &events, this);
}
bool Application::text_input_available() const { return impl->text_input != nullptr; }
void Application::text_input(bool enabled) { text_input(enabled, false); }
void Application::text_input(bool enabled, bool private_input) {
    impl->text_wanted = enabled;
    impl->text_private = private_input;
    impl->text_update();
}
void Application::on_text_input(std::function<void(const std::string &)> commit,
                                std::function<void(const std::string &)> preedit,
                                std::function<void(uint32_t, uint32_t)> erase) {
    impl->text_commit = std::move(commit);
    impl->text_preedit = std::move(preedit);
    impl->text_delete = std::move(erase);
}
} // namespace lmui
