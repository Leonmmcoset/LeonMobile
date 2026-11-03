#pragma once
#include <stdexcept>
namespace leon::appd {
inline void fail(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace leon::appd
