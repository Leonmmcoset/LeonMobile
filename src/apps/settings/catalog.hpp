#pragma once
#include <array>
#include <string_view>

namespace leon::apps::settings {
enum class Page {
    home,
    network,
    display,
    sound,
    storage,
    system,
    about,
    language,
    navigation,
    developer,
    security,
    applications,
    diagnostics,
    defaults,
    documents,
    datetime,
    notifications
};
enum class Icon {
    network,
    display,
    sound,
    storage,
    system,
    about,
    language,
    navigation,
    developer,
    applications
};
struct Entry {
    Page page;
    Icon icon;
    std::string_view chinese, english, keywords;
    bool home;
};
const std::array<Entry, 16> &entries();
const Entry &entry(Page page);
bool matches(const Entry &entry, std::string_view query);
} // namespace leon::apps::settings
