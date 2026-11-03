#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace leon::apps {
struct Directory {
    std::filesystem::path path;
    std::vector<std::filesystem::directory_entry> entries() const;
    void create(const std::string &name, bool folder) const;
};
} // namespace leon::apps
