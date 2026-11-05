#pragma once
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>
namespace leon::platform {
using Json = nlohmann::json;
// Calls have bounded timeouts and throw std::runtime_error on transport/service errors.
// The service authenticates SO_PEERCRED. No API parameter can select a caller UID.
class Client {
  public:
    explicit Client(std::string directory = "/run/leon") : directory_(std::move(directory)) {}
    Json settings() const;
    Json watch_settings(std::int64_t generation) const;
    Json set_preference(const std::string &key, Json value) const;
    Json network(std::int64_t since = -1) const;
    std::string choose_document(unsigned seconds = 300) const;
    std::string offer_document(const std::string &relative, unsigned seconds = 300) const;
    Json document(const std::string &token) const;
    std::vector<std::uint8_t> read_document(const std::string &token, std::size_t limit = 16 * 1024 * 1024) const;
    void revoke_document(const std::string &token) const;
    Json resolve(Json payload) const;
    std::string interact(Json payload) const;
    Json interaction(const std::string &token) const;
    Json receive() const;
    void acknowledge(const std::string &token) const;
    std::string open_audio(unsigned rate = 48000, unsigned channels = 2,
                           const std::string &focus = "exclusive", const std::string &title = {},
                           const std::string &artist = {}) const;
    std::size_t write_audio(const std::string &token, std::span<const std::uint8_t> s16le) const;
    void audio_control(const std::string &token, const std::string &operation) const;
    Json audio_status() const;
    void audio_volume(unsigned percent) const;
  private:
    std::string directory_;
    Json call(const std::string &service, const Json &request) const;
};
} // namespace leon::platform
