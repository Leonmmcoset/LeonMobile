#pragma once
#include <string>
namespace leon::apps {
struct Document {
    std::string path, text;
    void load();
    void save() const;
};
} // namespace leon::apps
