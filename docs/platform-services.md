# Platform services, protocol generation 1

The services in this document use the bounded, credential-checked C17 system
transport in `interfaces.md`. Client-supplied UIDs never select a caller. Native
fixtures use private roots and real Unix peer credentials. These protocols are
for the LeonMobile 1.0.0 developer graphics prototype.

## Settings

`settingsd` owns the five public UI preferences: boolean `dark` and
`reduce_motion`, `language` (`zh-CN`, `en`), `navigation` (`gestures`, `buttons`),
and integer `volume` (0–100, preferred playback volume). It stores a versioned
record with a durable generation in `/data/leon/system/settings.json`.
Root, the desktop UID 1000 and Settings UID 11000 may write; reads contain only
these nonsecret preferences. Authentication, root passwords and grants remain
in their dedicated services.

* `status` returns `settings` and `generation`.
* `schema` returns types, defaults, ranges and enum choices.
* `set` accepts `key`, `value`; `update` accepts a nonempty `values` object.
  Optional `expected_generation` implements compare-and-set. A batch is checked
  completely before any write. Unknown keys and wrong types fail.
* `watch` accepts nonnegative integer `since`, returning the current snapshot and
  `changed`. Poll at most once per second; disconnected clients can resynchronize
  without losing their last generation. Repeating an unchanged value does not
  advance the generation.

The first initialization migrates validated values from the legacy
`/data/leon/settings.json`, filling missing defaults. A malformed existing record
prevents startup instead of resetting user preferences. Replacements use fsync,
rename and parent-directory fsync; an I/O failure is reported. The supervised
`settings` unit starts before the application broker and shell.

## Documents

`documentd`, `/run/leon/documents.sock`, provides temporary read-only document
capabilities. `choose` requires an installed identity declaring `files.import`;
the trusted shell selects each individual file. This selection itself authorizes
that document, without giving the application directory access. The existing
permission portal's copy/import API remains compatible.

* `choose` creates a picker request and returns a 128-bit random hex `token`.
  Optional integer `seconds` is 1–600 (default 300), including selection time.
* Desktop/root `pending`, `files` (`directory` relative to `/data/leon/shared`),
  `pick` (`token`, relative `file`) and `cancel` implement the trusted picker.
* `selection` returns `state` (`choosing`, `ready`, `canceled`), `done`, display
  `name`, byte count and remaining lifetime. Tokens are visible only to their
  owner or trusted peers. Caller-supplied UIDs never override ownership.
* `read` takes `token`, nonnegative integer `offset` and `length` 1–2048, returning
  binary-safe lowercase `hex`, the next `offset` and `eof`.
* `offer` snapshots a relative `file` beneath the caller's own private home. It
  lets an app share its own document without exposing a private path to peers.
* `revoke` invalidates the owner's capability and all delegated descendants.
  Root-only `delegate` additionally takes verified `from_uid`, `to_uid`, and
  returns a distinct capability for the recipient. Root-only `reset_app` is the
  application broker's clear/uninstall hook. `list` shows the caller's grants.

Files must be regular, at most 16 MiB, and are opened beneath a directory FD with
openat2 NO_SYMLINKS/NO_MAGICLINKS. A sealed anonymous snapshot prevents changes
after selection. At most 32 capabilities, four per app and 64 MiB of accounted
snapshots are allowed. Grants expire, disappear on daemon restart and are bound
to the installer's instance marker (including same-version reinstalls).
Revocation blocks subsequent reads; bytes an app has already read cannot be
recalled. Applications never receive a raw file descriptor that bypasses later
revocation checks. Settings UID 11000 may list and revoke grants, but cannot
select documents for other apps or read their contents.

## Application interactions

`intentd`, `/run/leon/intents.sock`, resolves installed manifest `handlers`.
A handler has an `action` (`open`, `share`) and exactly one selector: `mime`
(e.g. `text/plain`, `image/*`) or `scheme` (e.g. `https`, `mailto`, open only).
There are at most 16 distinct handlers per manifest. Schemes are lowercase;
`file`, `data`, `javascript` and `shell` are forbidden. A declaration never
registers an executable command template or grants additional filesystem access.

`resolve` and `request` accept `action` and exactly one payload: `uri` (up to
2048 bytes, without control characters), `text` (up to 2048 bytes, `text/plain`),
or a `documentd` token in `document`. Text/documents require a concrete `mime`.
`resolve` returns `candidates` and the default selection `key`. An installed
application can `request` an interaction, returning its random `token`.
At most 32 requests and four per source app are retained, for 120 seconds.

Only desktop/root can inspect `pending`, `choose` (`token`, installed `target`,
optional boolean `always`), mark a delivered request `focused`, or dismiss other
apps' requests. Selection is checked again against current handlers. The service
delegates a distinct, owner-bound document capability before launching through
the application broker; it never passes a private filesystem path or arbitrary
process arguments. Each cross-service stage has a 350 ms absolute deadline and
runs separately from the request handler. A transport timeout while launching
retries the broker's idempotent launch operation with 500 ms backoff and a
five-second total deadline; explicit rejection fails immediately. A lost launch
reply therefore does not discard an interaction whose receiver was started.
Failure is observable through `result` and the trusted chooser's dismissible
error view.

The source polls `result` or uses `cancel`. The recipient polls `receive` for
events and acknowledges a token using `ack`; a failed acknowledgement can be
retried and unacknowledged events may be delivered again. Payloads are untrusted
application data. Receivers validate their content and should handle tokens
idempotently. Installation identities are rechecked before delivery and receipt.
Source cancellation/expiry also revokes delegated documents.

Defaults in `/data/leon/system/default-apps.json` bind a selector to an installed
instance. Only desktop/root/Settings can list `defaults`, `clear_default` with
`key`, or `set_default` with a resolvable payload and installed `target`.
`choose` with `always` is reserved for the trusted chooser. Changed or removed
installations cease to match; a manifest cannot silently take another app's
default. Root-only `reset_app` invalidates in-flight interactions during clear
or uninstall. The shell owns presentation/focus; an ordinary client cannot
impersonate the chooser. An unhandled URI reports no installed handler (the
prototype does not acquire a browser merely by adding this protocol).

## Audio

`audiod`, `/run/leon/audio.sock`, is the application audio policy and PCM broker.
PipeWire performs resampling/mixing and WirePlumber selects physical outputs,
including routing changes reported by its device policy. The service uses the
native PipeWire API, without spawning a helper for each audio request. It retries
backend connection failures every two seconds and invalidates streams after
backend loss; clients reopen and resynchronize rather than silently replaying.

Installed app identities can `open` a playback stream: integer `rate` 8000–96000
(default 48000), `channels` 1 or 2 (default 2), `focus` (`mix`, `exclusive`, `duck`,
default exclusive), and optional display `title`/`artist` (160/96 bytes).
Optional boolean `paused` (default false) starts a stream paused so the owner
can prebuffer PCM before resuming. The service returns a random `token`.
Only its owner can `write` frame-aligned
signed 16-bit little-endian PCM as `hex` (maximum 2048 decoded bytes per request).
The reply's `accepted` count implements backpressure; retry the remaining bytes.
The stream has a 64 KiB ring, with up to two streams per app and sixteen total.
`stream` reports buffered/played bytes, peak, underruns, volume and focus state.
`keepalive` (or another owner request) at least once every fifteen seconds retains
a stream; otherwise it expires. Install changes also invalidate it.

The latest active exclusive stream suspends earlier exclusive streams without
consuming their queues. Ducking reduces other streams to 20%; mixed streams can
play concurrently. `pause` releases focus, `resume` reacquires it, `toggle`
atomically switches the daemon's current pause state, and `close` frees
resources. These controls can also be called by desktop/root/Settings for media
controls. The shell uses `toggle` so repeated presses do not reuse an obsolete
state from a previous frame. `set_volume` takes the caller's own integer `volume` 0–100; trusted UI
can additionally select installed `app`. Policy is durable in
`/data/leon/system/audio.json`. Root-only `reset_app` closes streams and resets
its per-app volume during clear/uninstall.

`status` returns master volume/mute, backend availability and output `devices`.
Only trusted UI sees all streams and `current` media metadata; apps see their
own streams. Trusted `set_master` changes exactly one of `volume` or `muted`.
Master preferred volume is stored through settingsd, independently of whether
hardware is connected; mute is audiod's policy. Master percentages use
WirePlumber's cubic volume scale, matching `wpctl`; per-app PCM gain is linear.
Hardware sinks use the active PipeWire device Route's channel volume/mute;
software sinks use node Props. Route changes update the device/profile mapping,
so the next policy refresh targets the selected output.
`route` with an enumerated device
ID updates WirePlumber's configured default. Output removal and replacement use
the backend's standard routing policy. Microphone recording is not exposed.
Locking does not terminate playback; trusted lock-screen controls can pause it.
`--test` uses the same PCM/gain/focus logic with a deterministic in-memory sink,
never the host's sound device. Physical headphone routing requires hardware
validation; QEMU validates actual PipeWire playback and mixing separately.

## C++20 application SDK and desktop integration

`#include <leon/platform.hpp>` exposes `leon::platform::Client`. Link with
`find_package(LeonPlatform 1 CONFIG REQUIRED)` / `LeonPlatform::Platform`, or
`pkg-config leon-platform`. The generation-1 shared library is
`libleon-platform.so.1`, and requires C++20 and nlohmann JSON headers. Exceptions
report transport/service failure. SDK JSON replies follow the schemas above;
no SDK call accepts a caller identity. `Client` accepts an endpoint directory
only for isolated development. Unprivileged apps cannot mutate endpoint files.

Settings/watch and network/watch methods expose durable/resynchronizable
snapshots. Document methods choose, offer, inspect, read (with a caller-selected
byte limit) and revoke. `interact`, `resolve`, `receive`, `acknowledge` and
`interaction` implement delivery. `open_audio`, `write_audio`, `audio_control`,
`audio_status` and `audio_volume` expose playback and media state. `write_audio`
returns accepted bytes; callers retain/retry any remaining frames, poll focus,
and send keepalives during long pauses. Calls are synchronous and bounded; large
document reads and long tasks belong on an application worker, not its UI loop.

The trusted shell integrates document selection and application choosing into
compositor-enforced modal dialogs; prompts are withheld while locked. It can
remember Always selections, then focus the broker-launched receiver. The editor
handles received plain text/documents and saves a private draft. Images handles
PNG/JPEG documents; Files sends a document token when opening a private file.
Editor/Images also expose the new document picker. The editor's legacy Import
copy remains available for generation-1 compatibility. LMUI's demo includes a
60-second PCM test tone and text sharing, exercising real background playback.

Settings → Network edits Ethernet enablement, renewal, DHCP/static IPv4 and DNS.
Settings → Sound shows actual outputs and per-stream application volumes.
Settings → System → Default apps clears saved associations; Document access
lists and revokes temporary grants. Settings has a specific revoke/list role,
without gaining arbitrary document read access. Lock-screen and expanded-shade
media controls pause/resume; credential-protected lock screens hide track names.

The supervised `settings`, `documents`, `intents`, and `audio` units restart
independently. apps.sock's `settings`/`set` are compatibility relays to settingsd;
normal boot has no second preferences store. Explicit `--root` development brokers
retain their isolated legacy fixture, or use `--settings-socket PATH` to test the
relay. The legacy file is retained only as migration input. Clear/uninstall stop
writers and synchronously revoke document, interaction and audio state before
removing private data. Service failure is reported instead of claiming success.
