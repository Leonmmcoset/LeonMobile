#include "directory.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace leon::apps {
std::vector<std::filesystem::directory_entry> Directory::entries() const {
    std::vector<std::filesystem::directory_entry> result;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(
             path, std::filesystem::directory_options::skip_permission_denied, error))
        result.push_back(entry);
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.path() < b.path(); });
    return result;
}
void Directory::create(const std::string &name, bool folder) const {
    if (name.empty() || name.find('/') != std::string::npos || name == "..")
        throw std::runtime_error("Invalid filename");
    if (folder)
        std::filesystem::create_directory(path / name);
    else {
        std::ofstream file(path / name);
        if (!file)
            throw std::runtime_error("Cannot create file");
    }
}
} // namespace leon::apps
