#include "internal.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <set>
#include <sstream>
namespace leon::detail {
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
Json unpack_and_validate(const fs::path &filename, const fs::path &stage) {
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
            require(name != ".installation", "reserved installation metadata");
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
        std::vector<std::string> executables = {m.at("entry").get<std::string>()};
        if (m.contains("root"))
            executables.push_back(m["root"]["entry"].get<std::string>());
        for (const auto &entry_path : executables) {
            auto executable_path = stage / entry_path;
            require(fs::is_regular_file(executable_path), "application entry missing");
            unsigned char elf[64]{};
            std::ifstream exe(executable_path, std::ios::binary);
            exe.read(reinterpret_cast<char *>(elf), sizeof(elf));
            require(exe.gcount() == sizeof(elf) && !memcmp(elf, "\177ELF", 4) && elf[4] == 2 &&
                        elf[5] == 1 && elf[18] == 183 && elf[19] == 0 &&
                        (elf[16] == 2 || elf[16] == 3),
                    "entry must be a little-endian ARM64 ELF executable");
            fs::permissions(executable_path, fs::perms::owner_read | fs::perms::owner_write |
                                                 fs::perms::owner_exec | fs::perms::group_read |
                                                 fs::perms::group_exec | fs::perms::others_read |
                                                 fs::perms::others_exec);
        }
        archive_read_free(reader);
        return m;
    } catch (...) {
        archive_read_free(reader);
        throw;
    }
}
} // namespace leon::detail
