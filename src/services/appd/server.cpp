#include "../common/daemon.h"
#include "broker.hpp"
#include <cstdlib>
#include <sys/prctl.h>
namespace leon::appd {
int serve(Broker &broker, const std::string &socket_name) {
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    lm_daemon daemon{};
    daemon.options.socket_path = socket_name.c_str();
    daemon.data = &broker;
    daemon.tick = [](lm_daemon *d) { static_cast<Broker *>(d->data)->reap(); };
    daemon.handle = [](lm_daemon *d, uid_t uid, const lm_json *request) -> lm_json * {
        char *text = lm_dump(request);
        if (!text)
            return lm_failure("cannot decode request");
        try {
            auto parsed = Json::parse(text);
            free(text);
            text = nullptr;
            auto response = static_cast<Broker *>(d->data)->dispatch(parsed, uid).dump();
            return lm_parse(response.data(), response.size());
        } catch (const std::exception &e) {
            free(text);
            return lm_failure(e.what());
        }
    };
    return lm_serve(&daemon);
}
} // namespace leon::appd
