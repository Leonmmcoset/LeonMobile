#pragma once
#include "catalog.hpp"
#include <leon/mobile/ui.hpp>

namespace leon::apps::settings {
void icon(mobile::Canvas &canvas, Icon type, double x, double y, mobile::Color color);
void chevron(mobile::Canvas &canvas, double x, double y);
void radio(mobile::Canvas &canvas, double x, double y, bool selected);
void toggle(mobile::Canvas &canvas, mobile::Rect bounds, bool selected);
void information(mobile::Canvas &canvas, double y, const std::string &label,
                 const std::string &value);
} // namespace leon::apps::settings
