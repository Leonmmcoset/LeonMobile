#pragma once
#include "options.hpp"
namespace leon::appd {
// No callbacks into apps.sock. A successful reset guarantees Root writers are gone.
void revoke_root(const Options &, const std::string &id, bool reset = false);
} // namespace leon::appd
