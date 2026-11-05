#include "json.h"
#include "daemon.h"
#include <cstdlib>
#include <cstring>
#include <leon/package.hpp>
using J = leon::Json;
static const J *value(const lm_json *p) { return reinterpret_cast<const J *>(p); }
static J *value(lm_json *p) { return reinterpret_cast<J *>(p); }
static lm_json *wrap(J *p) { return reinterpret_cast<lm_json *>(p); }
extern "C" {
lm_json *lm_object() { return wrap(new J(J::object())); }
lm_json *lm_array() { return wrap(new J(J::array())); }
lm_json *lm_parse(const char *text, size_t size) {
    try {
        if (memchr(text, 0, size))
            return nullptr;
        auto check = [](int depth, J::parse_event_t event, J &part) {
            if (depth > 32)
                throw std::runtime_error("JSON nesting limit");
            if ((event == J::parse_event_t::key || event == J::parse_event_t::value) &&
                part.is_string() &&
                part.get_ref<const std::string &>().find('\0') != std::string::npos)
                throw std::runtime_error("embedded NUL");
            return true;
        };
        return wrap(new J(J::parse(text, text + size, check)));
    } catch (...) {
        return nullptr;
    }
}
char *lm_dump(const lm_json *json) {
    try {
        auto text = value(json)->dump() + "\n";
        return strdup(text.c_str());
    } catch (...) {
        return nullptr;
    }
}
void lm_free(lm_json *json) { delete value(json); }
const lm_json *lm_get(const lm_json *json, const char *key) {
    if (!json || !value(json)->is_object())
        return nullptr;
    auto it = value(json)->find(key);
    return it == value(json)->end() ? nullptr : reinterpret_cast<const lm_json *>(&*it);
}
const lm_json *lm_at(const lm_json *json, size_t index) {
    if (!json || !value(json)->is_array() || index >= value(json)->size())
        return nullptr;
    return reinterpret_cast<const lm_json *>(&(*value(json))[index]);
}
size_t lm_size(const lm_json *json) { return json ? value(json)->size() : 0; }
const char *lm_string(const lm_json *json, const char *fallback) {
    return json && value(json)->is_string() ? value(json)->get_ref<const std::string &>().c_str()
                                            : fallback;
}
int64_t lm_integer(const lm_json *json, int64_t fallback) {
    if (!json || !value(json)->is_number_integer())
        return fallback;
    if (value(json)->is_number_unsigned() && value(json)->get<uint64_t>() > INT64_MAX)
        return fallback;
    return value(json)->get<int64_t>();
}
int lm_boolean(const lm_json *json, int fallback) {
    return json && value(json)->is_boolean() ? int(value(json)->get<bool>()) : fallback;
}
int lm_is_array(const lm_json *json) { return json && value(json)->is_array(); }
int lm_is_object(const lm_json *json) { return json && value(json)->is_object(); }
int lm_is_string(const lm_json *json) { return json && value(json)->is_string(); }
void lm_set(lm_json *json, const char *key, lm_json *child) {
    (*value(json))[key] = std::move(*value(child));
    lm_free(child);
}
void lm_erase(lm_json *json, const char *key) { value(json)->erase(key); }
lm_json *lm_keys(const lm_json *json) {
    auto keys = J::array();
    if (json && value(json)->is_object())
        for (auto it = value(json)->begin(); it != value(json)->end(); ++it)
            keys.push_back(it.key());
    return wrap(new J(std::move(keys)));
}
void lm_append(lm_json *json, lm_json *child) {
    value(json)->push_back(std::move(*value(child)));
    lm_free(child);
}
void lm_set_string(lm_json *json, const char *key, const char *s) {
    (*value(json))[key] = s ? s : "";
}
void lm_set_integer(lm_json *json, const char *key, int64_t n) { (*value(json))[key] = n; }
void lm_set_boolean(lm_json *json, const char *key, int b) { (*value(json))[key] = bool(b); }
lm_json *lm_copy(const lm_json *json) { return json ? wrap(new J(*value(json))) : lm_object(); }
lm_json *lm_app_by_id(const char *root, const char *builtin, const char *id) {
    try {
        return wrap(new J(leon::Packages(root, builtin).lookup(id)));
    } catch (...) {
        return nullptr;
    }
}
int lm_grants_valid(const lm_json *json) {
    if (!json || !value(json)->is_object())
        return 0;
    for (auto it = value(json)->begin(); it != value(json)->end(); ++it) {
        if (!leon::valid_id(it.key()) || !it->is_object())
            return 0;
        for (auto permission = it->begin(); permission != it->end(); ++permission)
            if ((permission.key() != "network" && permission.key() != "files.import") ||
                !permission->is_boolean())
                return 0;
    }
    return 1;
}
lm_json *lm_app(const char *root, const char *builtin, uid_t uid) {
    try {
        for (const auto &app : leon::Packages(root, builtin).list())
            if (app.at("uid").get<uid_t>() == uid)
                return wrap(new J(app));
    } catch (...) {
    }
    return nullptr;
}
lm_json *lm_apps(const char *root, const char *builtin) {
    try {
        return wrap(new J(leon::Packages(root, builtin).list()));
    } catch (...) {
        return nullptr;
    }
}
lm_json *lm_app_identity(const char *root, const char *builtin, uid_t uid) {
    lm_json *app = lm_app(root, builtin, uid);
    if (!app)
        return nullptr;
    if (lm_boolean(lm_get(app, "builtin"), 0)) {
        lm_set_string(app, "installation", lm_string(lm_get(app, "version"), ""));
        return app;
    }
    char path[PATH_MAX], instance[34];
    const char *id = lm_string(lm_get(app, "id"), "");
    if (snprintf(path, sizeof(path), "%s/apps/%s/current/.installation", root, id) >=
            (int)sizeof(path) || lm_read_text(path, instance, sizeof(instance)) ||
        strlen(instance) != 32 || strspn(instance, "0123456789abcdef") != 32) {
        lm_free(app);
        return nullptr;
    }
    lm_set_string(app, "installation", instance);
    return app;
}
lm_json *lm_root_app(const char *root, const char *builtin, uid_t uid, pid_t pid) {
    try {
        return wrap(new J(leon::Packages(root, builtin).root_identity(uid, pid)));
    } catch (...) {
        return nullptr;
    }
}
}
