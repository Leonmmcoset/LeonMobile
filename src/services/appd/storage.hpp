#pragma once
#include <leon/package.hpp>
namespace leon::appd {
// Descriptor-relative walks never follow application-owned symbolic links.
Json storage_usage(const fs::path &path, bool missing_is_empty = true);
void erase_private_tree(const fs::path &parent, const std::string &name);
} // namespace leon::appd
