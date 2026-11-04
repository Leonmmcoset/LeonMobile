#pragma once
#include <leon/ipc.hpp>
namespace leon {
// One bounded request/reply to a named generation-1 system service.
// SO_PEERCRED authenticates callers; a supplied UID never changes identity.
Json system_request(const std::string &service, const Json &message);
} // namespace leon
