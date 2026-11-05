#include "notificationd.h"
#include "policy.hpp"
#include <cstdlib>
#include <cstring>
#include <leon/package.hpp>
#include <limits>
#include <stdexcept>

namespace {
using leon::Json;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

char *encode(const Json &reply) {
    auto text = reply.dump() + "\n";
    auto *result = static_cast<char *>(std::malloc(text.size() + 1));
    if (result)
        std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}

std::string application_for(const notification_options &options, uid_t caller) {
    // Share the installed/built-in identity policy with the launcher. Do not
    // infer identities from caller-supplied app IDs or duplicate the UID map.
    for (const auto &manifest : leon::Packages(options.root, options.builtin).list())
        if (manifest.at("uid").get<uid_t>() == caller)
            return manifest.at("id").get<std::string>();
    throw std::runtime_error("unknown application identity");
}
} // namespace

extern "C" char *notification_error(const char *message) {
    try {
        return encode({{"ok", false}, {"error", message}});
    } catch (...) {
        return nullptr;
    }
}

extern "C" char *notification_dispatch(notification_store *store,
                                       const notification_options *options, uid_t caller,
                                       const char *text, size_t size) {
    try {
        require(!std::memchr(text, '\0', size), "invalid JSON request");
        auto request = Json::parse(text, text + size);
        auto op = request.at("op").get<std::string>();
        if(op=="notifications_for") {
            require(caller==0,"permission denied");
            const auto &uid=request.at("uid");
            require(uid.is_number_integer() && (uid==0 || uid==1000),"invalid desktop UID");
            caller=uid.get<uid_t>();op="notifications";
        }
        if(op=="notification_policy" || op=="configure" || op=="app_policy" || op=="test_sound")
            return encode(leon::notifications::policy(*store,*options,caller,request));
        if (op == "notifications") {
            require(caller == 0 || caller == 1000, "permission denied");
            auto items = Json::array();
            std::string epoch;
            bool locked = caller!=0 && leon::notifications::locked(*options,epoch);
            auto visibility=leon::notifications::lockscreen(*store);
            for (size_t i = 0; i < store->count; ++i) {
                const auto *item = notification_store_at(store, i);
                if(!leon::notifications::enabled(*store,item->app) || (locked && visibility=="none")) continue;
                Json value={{"app",item->app},{"title",locked && visibility=="hide" ? "" : std::string(item->title,item->title_size)}};
                if(locked && visibility=="hide") value["redacted"]=true;
                items.push_back(std::move(value));
            }
            return encode({{"ok", true}, {"notifications", items}, {"privacy_locked",locked},{"lock_epoch",epoch}});
        }
        require(op == "notify" || op == "notify_for", "unknown operation");
        if (op == "notify_for") {
            // Only the root broker may relay an older client's peer UID.
            require(caller == 0, "permission denied");
            const auto &uid = request.at("uid");
            require(uid.is_number_integer() && uid >= 0 && uid <= std::numeric_limits<uid_t>::max(),
                    "invalid application UID");
            caller = uid.get<uid_t>();
        }
        auto app = application_for(*options, caller);
        auto title = request.at("title").get<std::string>();
        require(title.size() <= NOTIFICATION_TITLE_LIMIT, "notification too long");
        if(!leon::notifications::enabled(*store,app)) return encode({{"ok",true},{"suppressed",true}});
        notification_store_add(store, app.c_str(), title.data(), title.size());
        leon::notifications::alert(*store,app);
        return encode({{"ok", true}});
    } catch (const Json::parse_error &) {
        // A parser diagnostic may contain invalid input bytes, which cannot
        // themselves be encoded as a JSON error string.
        return notification_error("invalid JSON request");
    } catch (const std::exception &error) {
        return notification_error(error.what());
    } catch (...) {
        return notification_error("notification service error");
    }
}
