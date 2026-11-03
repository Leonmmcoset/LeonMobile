#include "package.hpp"
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace leon {
static void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Fd {
    int value;
    explicit Fd(int v) : value(v) {
        if (v < 0)
            throw std::runtime_error(std::strerror(errno));
    }
    ~Fd() { close(value); }
    operator int() const { return value; }
};
static void durable_dir(const fs::path &path) {
    Fd fd(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    require(!fsync(fd), "directory sync failed");
}
static void write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0 && errno == EINTR)
            continue;
        require(k > 0, "write failed");
        p += k;
        n -= size_t(k);
    }
}
bool valid_id(const std::string &id) {
    return id.size() <= 96 &&
           std::regex_match(id, std::regex("[a-z][a-z0-9]*(\\.[a-z][a-z0-9_-]*)+"));
}
Json read_json(const fs::path &path) {
    require(fs::file_size(path) <= 1024 * 1024, "JSON document too large");
    std::ifstream file(path);
    require(bool(file), "cannot read " + path.string());
    return Json::parse(file);
}
void atomic_json(const fs::path &path, const Json &data) {
    fs::create_directories(path.parent_path());
    auto temporary = path.string() + ".new";
    Fd fd(open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0644));
    std::string text = data.dump(2) + "\n";
    write_all(fd, text.data(), text.size());
    require(!fsync(fd), "JSON sync failed");
    fs::rename(temporary, path);
    durable_dir(path.parent_path());
}
static std::array<unsigned, 3> version(const std::string &s) {
    require(std::regex_match(
                s, std::regex("(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})")),
            "version must be numeric major.minor.patch");
    std::array<unsigned, 3> v;
    char dot;
    std::istringstream in(s);
    in >> v[0] >> dot >> v[1] >> dot >> v[2];
    return v;
}
static bool safe_path(const std::string &s) {
    if (s.empty() || s[0] == '/' || s.find('\\') != std::string::npos || s.size() > 240)
        return false;
    for (const auto &p : fs::path(s))
        if (p == ".." || p == "." || p.empty())
            return false;
    return true;
}
void validate_manifest(const Json &m, bool builtin) {
    require(m.is_object() && m.value("format", 0) == 1, "unsupported application format");
    require(valid_id(m.at("id").get<std::string>()), "invalid application ID");
    version(m.at("version").get<std::string>());
    require(m.at("arch") == "aarch64", "application architecture must be aarch64");
    require(m.value("lmui", 0) == 1, "unsupported LMUI ABI");
    require(m.at("name").is_string() && !m.at("name").get<std::string>().empty(),
            "missing application name");
    std::string entry = m.at("entry");
    require(builtin ? entry.starts_with("/usr/bin/") : safe_path(entry), "invalid executable path");
    if (m.contains("args")) {
        require(m["args"].is_array() && m["args"].size() <= 16, "invalid arguments");
        for (const auto &a : m["args"])
            require(a.is_string(), "invalid argument");
    }
}
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
static std::string digest(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    require(bool(f), "cannot hash file");
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    require(ctx, "digest allocation failed");
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    std::array<char, 65536> buf{};
    while (f) {
        f.read(buf.data(), buf.size());
        EVP_DigestUpdate(ctx, buf.data(), size_t(f.gcount()));
    }
    unsigned char hash[32];
    unsigned len;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream out;
    for (auto c : hash)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
    return out.str();
}
void Packages::install(const fs::path &filename) {
    fs::create_directories(root);
    Fd lock(
        open((root / "installer.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
    require(!flock(lock, LOCK_EX), "installer lock failed");
    fs::create_directories(root / "staging");
    /* Only the locked installer owns staging; interrupted transactions are safe to collect. */
    for (const auto &e : fs::directory_iterator(root / "staging"))
        fs::remove_all(e.path());
    fs::path stage = root / "staging" / std::to_string(getpid());
    fs::create_directory(stage);
    archive *reader = archive_read_new();
    archive_read_support_filter_gzip(reader);
    archive_read_support_format_tar(reader);
    try {
        require(archive_read_open_filename(reader, filename.c_str(), 65536) == ARCHIVE_OK,
                "cannot open package archive");
        archive_entry *entry;
        std::set<std::string> files;
        size_t bytes = 0;
        int rc;
        unsigned entries = 0;
        while ((rc = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
            require(++entries <= 8192, "excessive archive entries");
            std::string name = archive_entry_pathname(entry);
            require(safe_path(name), "unsafe archive path");
            require(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry),
                    "links are forbidden in application packages");
            auto kind = archive_entry_filetype(entry);
            require(kind == AE_IFDIR || kind == AE_IFREG, "special file forbidden");
            if (kind == AE_IFDIR) {
                fs::create_directories(stage / name);
                continue;
            }
            require(files.insert(name).second && files.size() <= 4096,
                    "duplicate or excessive archive entries");
            auto length = archive_entry_size(entry);
            require(length >= 0 && length <= 128 * 1024 * 1024, "file too large");
            bytes += size_t(length);
            require(bytes <= 256 * 1024 * 1024, "package too large");
            auto dest = stage / name;
            fs::create_directories(dest.parent_path());
            Fd fd(open(dest.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0644));
            std::array<char, 65536> buf;
            ssize_t n;
            size_t actual = 0;
            while ((n = archive_read_data(reader, buf.data(), buf.size())) > 0) {
                actual += size_t(n);
                require(actual <= size_t(length), "invalid file length");
                write_all(fd, buf.data(), size_t(n));
            }
            require(n == 0 && actual == size_t(length), "truncated archive entry");
            require(!fsync(fd), "file sync failed");
        }
        require(rc == ARCHIVE_EOF, "corrupt archive");
        auto m = read_json(stage / "manifest.json");
        validate_manifest(m);
        auto checksums = read_json(stage / "checksums.json");
        require(checksums.is_object(), "invalid checksum document");
        require(files.contains("manifest.json") && files.contains("checksums.json"),
                "missing package metadata");
        require(checksums.size() + 1 == files.size(), "checksum coverage mismatch");
        for (const auto &name : files)
            if (name != "checksums.json")
                require(checksums.contains(name) && checksums[name] == digest(stage / name),
                        "checksum mismatch: " + name);
        std::string id = m.at("id"), ver = m.at("version");
        for (const auto &app : list())
            if (app.at("id") == id) {
                require(!app.value("builtin", false), "cannot replace a system application");
                require(version(ver) > version(app.at("version")), "version must increase");
            }
        auto executable_path = stage / m.at("entry").get<std::string>();
        require(fs::is_regular_file(executable_path), "application entry missing");
        unsigned char elf[64]{};
        std::ifstream exe(executable_path, std::ios::binary);
        exe.read(reinterpret_cast<char *>(elf), sizeof(elf));
        require(exe.gcount() == sizeof(elf) && !memcmp(elf, "\177ELF", 4) && elf[4] == 2 &&
                    elf[5] == 1 && elf[18] == 183 && elf[19] == 0 && (elf[16] == 2 || elf[16] == 3),
                "entry must be a little-endian ARM64 ELF executable");
        fs::permissions(executable_path, fs::perms::owner_read | fs::perms::owner_write |
                                             fs::perms::owner_exec | fs::perms::group_read |
                                             fs::perms::group_exec | fs::perms::others_read |
                                             fs::perms::others_exec);
        Json identities = fs::exists(root / "identities.json")
                              ? read_json(root / "identities.json")
                              : Json{{"next", 12000}, {"apps", Json::object()}};
        if (!identities["apps"].contains(id)) {
            unsigned uid = identities.at("next");
            require(uid < 60000, "application identities exhausted");
            identities["apps"][id] = uid;
            identities["next"] = uid + 1;
            atomic_json(root / "identities.json", identities);
        }
        auto appdir = root / "apps" / id;
        fs::create_directories(appdir / "versions");
        auto dest = appdir / "versions" / ver;
        // An interrupted transaction may have published this unused version
        // directory before switching current. Validation above guarantees that
        // this version is newer than the active one, so it is safe to replace.
        if (fs::exists(dest))
            fs::remove_all(dest);
        durable_dir(stage);
        fs::rename(stage, dest);
        durable_dir(appdir / "versions");
        auto next = appdir / "next";
        fs::remove(next);
        fs::create_symlink(fs::path("versions") / ver, next);
        fs::rename(next, appdir / "current");
        durable_dir(appdir);
        archive_read_free(reader);
    } catch (...) {
        archive_read_free(reader);
        fs::remove_all(stage);
        throw;
    }
}
void Packages::remove(const std::string &id, bool purge) {
    require(valid_id(id), "invalid application ID");
    fs::create_directories(root);
    Fd lock(
        open((root / "installer.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
    require(!flock(lock, LOCK_EX), "installer lock failed");
    auto m = lookup(id);
    require(!m.value("builtin", false), "cannot remove a system application");
    auto appdir = root / "apps" / id;
    fs::remove(appdir / "current");
    durable_dir(appdir);
    fs::remove_all(appdir);
    if (purge)
        fs::remove_all(root / "data" / id);
}
std::string request(const std::string &socket_path, const Json &message) {
    Fd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    timeval timeout{message.value("op", "") == "install" ? 60 : 3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    require(socket_path.size() < sizeof(address.sun_path), "socket path too long");
    strcpy(address.sun_path, socket_path.c_str());
    require(!connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)),
            "service unavailable: " + socket_path);
    auto text = message.dump() + "\n";
    write_all(fd, text.data(), text.size());
    shutdown(fd, SHUT_WR);
    std::string result;
    std::array<char, 4096> buf;
    ssize_t n;
    while ((n = read(fd, buf.data(), buf.size())) > 0) {
        result.append(buf.data(), size_t(n));
        require(result.size() < 1024 * 1024, "response too large");
    }
    require(n == 0, "service response timed out");
    return result;
}
} // namespace leon
