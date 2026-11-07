#include "application.hpp"
#include <leon/platform.hpp>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>

using namespace mobile;
namespace leon::apps {
InstallerApplication::InstallerApplication()
    : ApplicationWindow("org.leon.installer", "应用安装器", "App installer") {}

void InstallerApplication::request(Json message) {
    if (job.valid()) return;
    error.clear();
    job = std::async(std::launch::async, [message = std::move(message)] { return rpc(message); });
    app.redraw();
}
void InstallerApplication::choose() {
    if (job.valid() || !picker.empty()) return;
    error.clear();
    activity = "choosing";
    job = std::async(std::launch::async, [] {
        return Json{{"picker", leon::platform::Client().choose_document(600)}};
    });
}
void InstallerApplication::import_package(const std::string &document, const std::string &event) {
    if (job.valid()) return;
    activity = "reading"; error.clear(); scroll = 0;
    auto home = user_home();
    job = std::async(std::launch::async, [document, event, home] {
        leon::platform::Client client;
        auto incoming = fs::path(home) / "incoming.lmpkg";
        int fd = -1;
        bool prepare_attempted = false;
        auto acknowledge = [&] {
            try { if (!event.empty()) client.acknowledge(event); } catch (...) {}
            try { client.revoke_document(document); } catch (...) {}
        };
        try {
            auto info = client.document(document);
            // The shared SDK validates size, binary encoding and capability
            // ownership. The broker separately validates the sealed package.
            auto data = client.read_document(document, 16 * 1024 * 1024);
            unlink(incoming.c_str());
            fd = open(incoming.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (fd < 0) throw std::runtime_error("Cannot stage selected package");
            size_t offset = 0;
            while (offset < data.size()) {
                auto n = write(fd, data.data() + offset, data.size() - offset);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) throw std::runtime_error("Not enough storage for selected package");
                offset += size_t(n);
            }
            if (fsync(fd)) throw std::runtime_error("Cannot save selected package");
            close(fd); fd = -1;
            prepare_attempted = true;
            auto result = rpc({{"op", "package_prepare"}});
            unlink(incoming.c_str());
            acknowledge();
            return Json{{"transaction", result}, {"source", info.at("name")}};
        } catch (...) {
            if (fd >= 0) close(fd);
            unlink(incoming.c_str());
            acknowledge();
            // A lost prepare reply can leave a valid review; it still requires
            // an explicit user confirmation after status is resynchronized.
            if (prepare_attempted) {
                try {
                    auto status = rpc({{"op", "package_status"}});
                    if (status.at("state") != "idle") return Json{{"transaction", status}};
                } catch (...) {}
            }
            throw;
        }
    });
    app.redraw();
}
void InstallerApplication::cancel() {
    picker.clear(); activity.clear(); source.clear(); scroll = 0;
    request({{"op", "package_cancel"}, {"token", transaction.value("token", "")}});
}
void InstallerApplication::on_tick() {
    if (job.valid()) {
        if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try {
            auto result = job.get();
            if (result.contains("picker")) picker = result.at("picker");
            else {
                transaction = result.contains("transaction") ? result.at("transaction") : result;
                if (result.contains("source")) source = result.at("source");
                activity.clear();
            }
            error.clear();
        } catch (const std::exception &e) {
            error = e.what(); activity.clear(); initialized = false;
        }
        app.redraw();
        return;
    }
    if (++ticks % 5) return;
    auto state = transaction.value("state", "idle");
    if (!initialized || state == "checking" || state == "installing") {
        initialized = true;
        request({{"op", "package_status"}});
        return;
    }
    try {
        leon::platform::Client client;
        if (!picker.empty()) {
            auto info = client.document(picker);
            if (info.at("state") == "canceled") {
                client.revoke_document(picker); picker.clear(); activity.clear(); app.redraw();
            } else if (info.at("state") == "ready") {
                auto document = picker; picker.clear();
                import_package(document);
            }
        } else if (state == "idle" || state == "installed" || state == "failed") {
            auto events = client.receive();
            if (events.empty()) return;
            auto event = events.front();
            auto payload = event.at("payload");
            auto token = event.at("token").get<std::string>();
            if (payload.value("action", "") != "open" ||
                payload.value("mime", "") != "application/vnd.leonmobile.package" ||
                !payload.contains("document")) { client.acknowledge(token); return; }
            import_package(payload.at("document"), token);
        }
    } catch (const std::exception &e) {
        if (!picker.empty()) { picker.clear(); activity.clear(); error = e.what(); app.redraw(); }
    }
}
} // namespace leon::apps
