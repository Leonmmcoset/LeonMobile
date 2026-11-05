# LeonMobile platform SDK generation 1

Include `<leon/platform.hpp>`, use `leon::platform::Client`, and link
`LeonPlatform::Platform` after `find_package(LeonPlatform 1 CONFIG REQUIRED)`.
Alternatively use `pkg-config --cflags --libs leon-platform`. The ABI is
`libleon-platform.so.1`; applications use C++20 and nlohmann JSON headers.
Buildroot installs the SDK to staging through `leon-core`, and graphical packages
consume its exported target rather than installing duplicate shared libraries.
`examples/platform` builds against only the installed SDK.

The client exposes settings and network snapshots/watches; document selection,
private document offers, bounded reads and revocation; interaction resolution,
submission, receipt and acknowledgement; PCM playback, focus, per-app volume and
media controls. No call lets an app select another caller UID. Services check
Unix peer credentials and installed identities; calls throw on service/transport
failure. An alternate socket directory is provided for isolated development.

```cpp
leon::platform::Client system;
auto preferences = system.settings();
auto generation = preferences.at("generation").get<std::int64_t>();
// On a timer, at most once per second:
auto changes = system.watch_settings(generation);

// Opens a trusted system picker. Poll document(token) for ready/canceled.
auto token = system.choose_document();
// Once ready, read on a worker, with the application's own size limit:
auto bytes = system.read_document(token, 1024 * 1024);
system.revoke_document(token);

// The system chooses among installed manifest handlers.
auto request = system.interact({{"action", "share"}, {"mime", "text/plain"},
                                {"text", "Hello from LeonMobile"}});
```

Examples are separate operations; wait for user selection before reading.
`receive()` returns interaction events. Handle a token idempotently and
`acknowledge(token)` only after processing it; unacknowledged delivery can repeat.
Use a document capability, never an absolute private path, for cross-app files.

`open_audio(rate, channels, focus, title, artist)` returns a stream token.
`write_audio(token, bytes)` takes frame-aligned s16le PCM and returns accepted
bytes. Keep any unaccepted bytes and retry without blocking the UI. Use
`audio_control(token, "keepalive")` during long pauses and `"close"` on shutdown.
Calls are synchronous with bounded transport timeouts. Large document reads
belong on workers. Service restart invalidates temporary document/audio tokens;
reconnect and obtain a new selection/stream instead of assuming persistence.

Exact schemas, authorization and limits are in
[platform-services.md](platform-services.md). A signature verification mechanism
is not added by this SDK.
