#include "document.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace leon::apps {
void Document::load() {
    if (std::filesystem::file_size(path) > 1024 * 1024)
        throw std::runtime_error("File exceeds 1 MiB");
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open file");
    text.assign(std::istreambuf_iterator<char>(file), {});
}
void Document::save() const {
    std::ofstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open file");
    file << text;
    if (!file)
        throw std::runtime_error("Write failed");
}
} // namespace leon::apps
