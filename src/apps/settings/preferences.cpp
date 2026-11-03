#include "application.hpp"
#include "widgets.hpp"
#include <cmath>

using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
void SettingsApplication::paint_preferences(Canvas &c) {
    auto t = c.theme;
    double w = c.width;
    if (page_ == Page::display) {
        c.rounded({20, 164, w - 40, 100}, t.container, 26);
        c.text(tr(en, "使用深色主题", "Use dark theme"), {40, 180, w - 148, 32}, 19, t.text, true);
        c.text(summary(Page::display), {40, 216, w - 148, 24}, 14, t.muted);
        settings::toggle(c, {w - 96, 200, 52, 30}, app.dark, "settings/dark");
        content_hit({20, 164, w - 40, 100},
                    [this](double, double) { save_preference("dark", !app.dark); });
        c.text(tr(en, "外观", "Appearance"), {28, 292, w - 56, 32}, 14, t.primary, true);
        double card = (w - 56) / 2;
        for (bool dark : {false, true}) {
            double x = dark ? 36 + card : 20;
            auto preview = Theme::material(dark);
            double chosen = c.animate(dark ? "settings/theme/dark" : "settings/theme/light",
                                      app.dark == dark ? 1 : 0, motion::settle);
            c.rounded({x, 340, card, 190}, mix(t.surface, t.container, chosen), 24);
            c.rounded({x + 16, 356, card - 32, 110}, preview.background, 14);
            c.rounded({x + 28, 372, card - 56, 20}, preview.container, 10);
            c.rounded({x + 28, 402, card - 76, 8}, preview.primary, 4);
            c.rounded({x + 28, 420, card - 56, 8}, preview.outline, 4);
            c.rounded({x + 28, 438, card - 96, 8}, preview.outline, 4);
            settings::radio(c, x + 30, 497, app.dark == dark,
                            dark ? "settings/theme/dark/radio" : "settings/theme/light/radio");
            c.text(dark ? tr(en, "深色", "Dark") : tr(en, "浅色", "Light"),
                   {x + 50, 478, card - 64, 38}, 16, t.text);
            content_hit({x, 340, card, 190},
                        [this, dark](double, double) { save_preference("dark", dark); });
        }
        c.text(tr(en, "主题将应用于系统界面与应用。", "Applies to the system and apps."),
               {28, 552, w - 56, 56}, 14, t.muted);
        c.rounded({20, 628, w - 40, 100}, t.surface, 26);
        c.text(tr(en, "减少动态效果", "Reduce motion"), {40, 642, w - 148, 32}, 19, t.text);
        c.text(tr(en, "保留即时触控反馈", "Keep immediate touch feedback"), {40, 678, w - 148, 28},
               13, t.muted);
        settings::toggle(c, {w - 96, 663, 52, 30}, app.reduce_motion, "settings/reduce-motion");
        content_hit({20, 628, w - 40, 100}, [this](double, double) {
            save_preference("reduce_motion", !app.reduce_motion);
        });
    } else if (page_ == Page::sound) {
        if (!touch_.continuous())
            volume_preview_ = -1;
        int volume = volume_preview_ >= 0 ? volume_preview_ : prefs.value("volume", 70);
        double position = c.animate("settings/volume", volume / 100.0,
                                    touch_.continuous() ? MotionSpec{0} : motion::settle);
        double thumb =
            c.animate("settings/volume/thumb", touch_.continuous() ? 13 : 10, motion::feedback);
        c.rounded({20, 164, w - 40, 200}, t.container, 28);
        settings::icon(c, settings::Icon::sound, 54, 198, t.primary);
        c.text(tr(en, "播放音量", "Playback volume"), {80, 180, w - 120, 36}, 17, t.text);
        c.text(std::to_string(volume) + "%", {44, 222, w - 88, 52}, 36, t.text);
        double start = 52, length = w - 104, y = 304;
        c.rounded({start, y - 4, length, 8}, t.outline, 4);
        if (position > 0)
            c.rounded({start, y - 4, length * position, 8}, t.primary, 4);
        c.circle(start + length * position, y, thumb, t.primary);
        c.text("0%", {44, 328, 50, 24}, 12, t.muted);
        c.text("100%", {w - 106, 328, 62, 24}, 12, t.muted, false, 2);
        auto count = hits.size();
        content_hit({start - 12, y - 24, length + 24, 48}, [this, start, length](double x, double) {
            volume_preview_ = int(std::lround(std::clamp((x - start) / length, 0.0, 1.0) * 100));
        });
        if (hits.size() > count) {
            hits.back().continuous = true;
            hits.back().action = [this] {
                save_preference("volume", volume_preview_);
                volume_preview_ = -1;
            };
        }
        c.rounded({20, 388, w - 40, 76}, t.surface, 22);
        c.text(tr(en, "当前播放设备", "Current playback device"), {36, 396, w - 72, 28}, 16,
               t.text);
        c.text(tr(en, "音量将应用于默认音频输出。", "Volume applies to the default audio output."),
               {36, 426, w - 72, 28}, 13, t.muted);
    } else if (page_ == Page::system) {
        link(c, {16, 164, w - 32, 76}, Page::language);
        link(c, {16, 248, w - 32, 76}, Page::navigation);
    } else if (page_ == Page::language) {
        for (bool english : {false, true}) {
            double y = english ? 248 : 164;
            c.rounded({20, y, w - 40, 76}, t.surface, 22);
            settings::radio(c, 50, y + 38, en == english,
                            english ? "settings/language/en" : "settings/language/zh");
            c.text(english ? "English" : "简体中文", {80, y + 8, w - 112, 32}, 19, t.text);
            c.text(english ? tr(en, "英语", "English") : tr(en, "简体中文", "Simplified Chinese"),
                   {80, y + 42, w - 112, 24}, 13, t.muted);
            content_hit({20, y, w - 40, 76}, [this, english](double, double) {
                save_preference("language", english ? "en" : "zh-CN");
            });
        }
    } else if (page_ == Page::navigation) {
        double card = (w - 56) / 2;
        bool gestures = prefs.value("navigation", "gestures") == "gestures";
        for (bool gesture : {true, false}) {
            double x = gesture ? 20 : 36 + card;
            double chosen = c.animate(gesture ? "settings/nav/gestures" : "settings/nav/buttons",
                                      gestures == gesture ? 1 : 0, motion::settle);
            c.rounded({x, 164, card, 244}, mix(t.surface, t.container, chosen), 24);
            double center = x + card / 2;
            c.rounded({center - 42, 184, 84, 140}, t.outline, 13);
            c.rounded({center - 39, 187, 78, 134}, t.background, 10);
            c.rounded({center - 29, 203, 58, 30}, t.container, 8);
            c.rounded({center - 29, 242, 44, 6}, t.muted, 3);
            c.rounded({center - 29, 255, 58, 6}, t.outline, 3);
            if (gesture)
                c.rounded({center - 17, 309, 34, 3}, t.text, 1.5);
            else {
                c.line(center - 24, 305, center - 29, 309, t.text, 1.5);
                c.line(center - 29, 309, center - 24, 313, t.text, 1.5);
                c.circle(center, 309, 3, t.text);
                c.rounded({center + 22, 306, 6, 6}, t.text, 1);
            }
            settings::radio(c, center, 348, gestures == gesture,
                            gesture ? "settings/nav/gestures/radio" : "settings/nav/buttons/radio");
            c.text(gesture ? tr(en, "手势导航", "Gestures") : tr(en, "三键导航", "3 buttons"),
                   {x + 8, 370, card - 16, 28}, 16, t.text, false, 1);
            content_hit({x, 164, card, 244}, [this, gesture](double, double) {
                save_preference("navigation", gesture ? "gestures" : "buttons");
            });
        }
        c.text(tr(en, "选择屏幕底部的系统导航方式。",
                  "Choose how to navigate at the bottom of the screen."),
               {28, 430, w - 56, 60}, 14, t.muted);
    }
}
} // namespace leon::apps
