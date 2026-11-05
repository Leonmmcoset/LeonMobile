#include "root.hpp"
#include "auth.h"
#include "internal.hpp"
#include <leon/package.hpp>
namespace leon::appd {
void revoke_root(const Options &options, const std::string &id, bool reset) {
    auto text = Json{{"op", reset ? "app_reset" : "app_cancel"}, {"id", id}}.dump() + "\n";
    char response[LEON_AUTH_LIMIT];
    fail(!auth_exchange(options.root_socket.c_str(), text.c_str(), response, 2200),
         "Root process cleanup service unavailable; retry");
    auto reply = Json::parse(response);
    if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Root process cleanup failed"));
}
} // namespace leon::appd
