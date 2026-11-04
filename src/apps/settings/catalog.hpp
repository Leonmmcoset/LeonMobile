#pragma once
#include <array>
#include <string_view>

namespace leon::apps::settings {
enum class Page { home, network, display, sound, storage, system, about, language, navigation, developer };
enum class Icon { network, display, sound, storage, system, about, language, navigation, developer };
struct Entry {
    Page page;
    Icon icon;
    std::string_view chinese, english, keywords;
    bool home;
};
const std::array<Entry, 9> &entries();
const Entry &entry(Page page);
bool matches(const Entry &entry, std::string_view query);
} // namespace leon::apps::settings
