#include "internal.hpp"
#include <algorithm>
#include <set>
namespace leon {
using namespace detail;
Packages::Packages(fs::path path, fs::path system)
    : root(std::move(path)), builtin(std::move(system)) {}
Json Packages::list() const {
    Json apps = Json::array();
    std::set<std::string> ids;
    if (fs::exists(builtin))
        for (const auto &p : fs::directory_iterator(builtin))
            if (p.path().extension() == ".json") {
                auto m = read_json(p.path());
                validate_manifest(m, true);
                m["builtin"] = true;
                m["uid"] = 11000 + unsigned(apps.size());
                ids.insert(m.at("id").get<std::string>());
                apps.push_back(m);
            }
    auto dir = root / "apps";
    if (fs::exists(dir))
        for (const auto &p : fs::directory_iterator(dir))
            if (fs::exists(p.path() / "current" / "manifest.json")) {
                auto m = read_json(p.path() / "current" / "manifest.json");
                validate_manifest(m);
                require(!ids.contains(m.at("id").get<std::string>()),
                        "application ID collides with system app");
                m["builtin"] = false;
                auto identities = read_json(root / "identities.json");
                m["uid"] = identities.at("apps").at(m.at("id").get<std::string>());
                apps.push_back(m);
            }
    /* Builtin UID is tied to the app ID, never directory enumeration order. */
    static const std::vector<std::string> names = {"settings", "calculator", "gallery", "files",
                                                   "editor",   "terminal",   "images",  "clock"};
    for (auto &m : apps)
        if (m.value("builtin", false)) {
            auto id = m.at("id").get<std::string>();
            auto it = std::find(names.begin(), names.end(), id.substr(id.rfind('.') + 1));
            require(it != names.end(), "unknown builtin identity");
            m["uid"] = 11000 + unsigned(it - names.begin());
        }
    std::sort(apps.begin(), apps.end(),
              [](const auto &a, const auto &b) { return a.at("id") < b.at("id"); });
    return apps;
}
Json Packages::lookup(const std::string &id) const {
    require(valid_id(id), "invalid application ID");
    for (const auto &m : list())
        if (m.at("id") == id)
            return m;
    throw std::runtime_error("application not installed");
}
fs::path Packages::executable(const Json &m) const {
    return m.value("builtin", false) ? fs::path(m.at("entry").get<std::string>())
                                     : root / "apps" / m.at("id").get<std::string>() / "current" /
                                           m.at("entry").get<std::string>();
}
} // namespace leon
