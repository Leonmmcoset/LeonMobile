#pragma once
#include <leon/mobile/ui.hpp>

namespace leon::shell::shade_ui {
enum class Icon { network, theme, sound, lock, settings, power, chevron, bell };
void icon(lmui::Canvas &canvas, Icon type, double x, double y, lmui::Color color,
          lmui::Color background, bool enabled = true);
void tile(lmui::Canvas &canvas, lmui::Rect bounds, Icon icon, const std::string &title,
          const std::string &subtitle, bool selected, bool enabled = true);
} // namespace leon::shell::shade_ui
