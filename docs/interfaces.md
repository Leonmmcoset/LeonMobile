# Version 1 interfaces

Unified log ingestion/query, credentials, quotas, history and `leonlog` are
specified in [logging.md](logging.md). The endpoints are independent of the
generic `lmsys` transport and do not inherit desktop-wide trust.

## Service configuration and control

Each UTF-8 `.toml` file in `/etc/leon/services` describes one foreground service. The parser implements a strict TOML subset: one supported assignment per line, double-quoted strings with quote/backslash escapes, single-line string arrays, decimal IDs, booleans, blank lines and whole-line comments. It rejects unknown/duplicate keys, missing dependencies, duplicate service names and dependency cycles. General TOML tables, multiline strings and trailing comments are not accepted.

```toml
name = "example"
type = "simple"
command = ["/usr/bin/example", "--foreground"]
after = ["prepare"]
uid = 1000
gid = 1000
restart = "on-failure"
restart_limit = 5
autostart = true
```

`type` is `simple` (default), `oneshot`, or `notify`. A oneshot dependency is satisfied only after successful completion; a simple dependency must have a running process. A notify dependency must explicitly declare readiness. A failed dependency blocks new dependents. Command execution uses an argument vector, without implicit shell interpretation. Explicit `/bin/sh -c` is possible for reviewed system service definitions.

Notify services inherit `LEON_NOTIFY_FD`, a nonblocking datagram socket, and send
the exact packet `READY=1` after initialization. Until then, status is `starting`.
`ready_timeout_ms` defaults to 10000 (range 1–60000). Optional `watchdog_ms`
(250–60000; zero disables it) requires `WATCHDOG=1` packets from the service's
actual event loop. Only the configured UID and foreground main PID are accepted;
forked descendants cannot forge readiness or heartbeats. The private helper
`serviced/notify.h` marks the descriptor close-on-exec when used. Services must
not leak it to launched applications. Ready/watchdog expiry sends SIGTERM,
escalates to SIGKILL after three seconds and applies the normal restart limit
and backoff. Exit codes 124 and 125 identify ready/watchdog timeout. Status adds
`health=starting|ready|ready-timeout|watchdog-timeout|stopping|inactive`.
Oneshots cannot declare watchdogs. Existing simple services remain compatible.

`restart` is `never` (default), `on-failure`, or `always`. Rapid failures use exponential delay capped at 32 seconds and a configurable retry count; the count resets after 30 seconds of runtime. Stop sends SIGTERM to the service group and escalates after three seconds. During shutdown, a dependency remains alive until its dependent services exit; independent services can stop concurrently. The manager caps total shutdown at 30 seconds, and PID 1 allows 35 seconds for it to finish. The parser accepts IDs and retry limits in the range 0–60000. Default identity is root; non-root services have supplementary groups cleared.

`/run/leon/services.sock` accepts one newline-terminated text command per connection. `status [name]` returns tab-separated service state, PID, exit code and retry count. `start name`, `stop name`, `restart name`, `reload name`, `poweroff`, and `reboot` require root and return `OK` or `ERR ...`. The client is `leonctl`. No network listener is exposed. This is a local v1 control interface, not a stable remote API.

`reload` applies the root-owned developer override in
`/data/leon/developer/services/NAME.json` (or `leond --policy-dir DIR` in tests).
It contains exactly `autostart`, `restart` and `restart_limit`, validated against
the descriptor's types/ranges. Restart policy changes immediately; autostart is
read on next manager start. Reload does not restart or stop the current process.
The independent root broker, its peer/password authorization, PTY FD transport
and developer operations are specified in [root-access.md](root-access.md).

## Application broker

`/run/leon/apps.sock` accepts one JSON object followed by a newline, up to 8191 bytes. Responses are newline-terminated JSON with `ok`; failures include `error`. Transport callers must handle partial reads/writes and wait for EOF. Credentials come from the socket peer, never request fields.

The shared C++ IPC client sends with `MSG_NOSIGNAL`: a daemon disconnect during
a send is a request exception, independent of the application's SIGPIPE handler.
It retains bounded send/receive timeouts and a 1 MiB response limit.

| Operation | Fields | Caller |
| --- | --- | --- |
| `list` | none | root/desktop |
| `launch`, `stop` | `id` | root/desktop |
| `close` | `id` (asynchronous task dismissal) | root/desktop |
| `foreground` | running `id` | root/desktop |
| `save` | running cooperative `id` | root/desktop |
| `background`, `lifecycle` | none | root/desktop |
| `install` | absolute `file` path | root/desktop |
| `remove` | `id`, optional `purge` | root/desktop |
| `app_list` | none | root/desktop/settings app |
| `app_info` | installed `id` | root/desktop/settings app |
| `app_stop`, `app_clear`, `app_uninstall` | installed `id` | root/desktop/settings app |
| `settings` | none | any connected local user |
| `set` | `key`, `value` | root/desktop/settings app |
| `notify` | `title` up to 160 UTF-8 bytes | installed application identity |
| `notifications` | none | root/desktop |
| `poweroff`, `reboot` | none | root/desktop |

`notify` and `notifications` on this endpoint are compatibility relays to
notificationd. The broker forwards the original socket peer UID through the
root-only `notify_for` operation and never accepts a claimed UID from the app.
It holds no notification state and does not queue requests when notificationd
is unavailable. New clients should connect to the notification endpoint below.
`leon-launchd --notifications-socket PATH` selects an isolated relay destination
for development; the default is `/run/leon/notifications.sock`.

The application management operations, their storage accounting and destructive
action semantics are specified in [application-management.md](application-management.md).
`leon-launchd --portal-socket PATH` selects an isolated permission service for
development. The production default is `/run/leon/portal.sock`.

Settings are `dark` (boolean), `reduce_motion` (boolean), `clock_24h` (boolean, default true), `language` (`zh-CN` or `en`), `navigation` (`gestures` or `buttons`), and `volume` (integer 0–100). Preferences are owned by settingsd; audiod applies volume to the selected PipeWire sink and retains the preference while outputs are unavailable. Defaults are light theme, full motion, simplified Chinese, gestures and a preferred volume of 70. Existing stores without `reduce_motion` read as false. The stored volume represents the user's last requested volume.

### Application lifecycle v1

Manifests may declare `"lifecycle":1`. The launcher then passes a private
nonblocking `SOCK_SEQPACKET` descriptor in `LEON_LIFECYCLE_FD`; the client marks
it close-on-exec. One packet is `SEQUENCE EVENT`, with an increasing unsigned
sequence and `foreground`, `background`, `save`, or `shutdown`. A client replies
`SEQUENCE ok` or `SEQUENCE failed` after processing the event. Late or superseded
acks do not acknowledge a new request. Foreground selection backgrounds the
previous app; home/tasks/lock background it without stopping it. A transient
notification shade retains the app's foreground state.

`lifecycle` reports PID, foreground/cooperative/responsive/pending flags and
the last event for each app, plus the latest 64 observed exit records.
It also reports `stopping` and `stop_phase` (`running`, `shutdown`, `term`, or
`kill`). `list` includes the same per-app state in `applications` alongside
the existing `apps` and `running` fields, so task rows and PIDs share one snapshot.
`lmpkg lifecycle` prints this state. Missing a two-second acknowledgement marks
the app unresponsive, rather than killing an otherwise usable app. Stop/remove
requests first send `shutdown`, allow two seconds for state save and exit, then
SIGTERM for one second before SIGKILL. Broker shutdown sends all cooperative
requests together with a two-second aggregate grace. Old manifests remain
supported through signal-based termination, with no required SDK change.

`close` / `lmpkg close APP_ID` starts this exit sequence without waiting for it;
success means the request was accepted. The event loop continues serving other
apps while the deadline runs. Repeated closes and closes of an absent app are
idempotent and do not extend deadlines. Closing drops foreground priority and
rejects launch, foreground and save requests for that instance. An acknowledgement
confirms handling of `shutdown`, not process exit: the task remains `stopping`
until `waitpid` observes its exit and records the actual status. Both cooperative
and forced exits appear once in exit history. Legacy apps skip checkpoint grace
and receive SIGTERM immediately. `stop` and `remove` retain synchronous behavior,
with a four-second wait bound and a failure if the kernel has not reaped the app;
an outstanding close continues in the event loop after that failure. Signals
cannot guarantee immediate exit for a process stuck in uninterruptible kernel I/O.

`lmpkg save APP_ID` dispatches an explicit checkpoint request without changing
foreground state or restarting the application. Its successful reply confirms
dispatch and includes lifecycle state; it does not claim the app has already
saved. Poll `lifecycle` until that app's `event` is `save` and `pending` is false,
then inspect `responsive` for the acknowledgement result. A later lifecycle
transition can supersede that observation. Missing apps and legacy manifests
without a lifecycle channel reject explicit saves. Timeout marks the app
unresponsive and leaves it running; normal stop retains bounded escalation.

`Application::on_lifecycle(callback)` runs on the LMUI event loop and returns a
boolean acknowledgement. Default shutdown exits the loop; callbacks can save
state and report a failure. It adds private state without changing the public
Application layout or generation 1 ABI. Built-in app support atomically writes
a versioned `.session.json` under the private app home on background, save,
shutdown and normal exit. Calculator state, editor drafts, file navigation and
LMUI component state restore on the next launch. Terminal child sessions are
ephemeral; lifecycle does not replay commands or reconstruct a dead PTY.
The root-only `reclaim` broker operation selects a background cooperative app
for the same checkpoint/exit sequence, excluding the foreground and legacy
apps. It returns the reclaimed ID or an empty string when no candidate exists.
Foreground/background transitions also adjust Linux `oom_score_adj` on target
so background processes are preferred by the kernel OOM selector.

The trusted shell controls focus through `/run/leon/wm.sock`, mode 0600, using
`shell` or `focus APP_ID`. `lock` / `unlock` and `modal` / `unmodal` enforce
session-lock and permission-dialog input routing in the compositor. While either
restriction is set, application focus requests and new windows cannot take
keyboard focus, and hit testing routes to the trusted shell. The compositor also
checks peer UID. Ordinary application users cannot invoke these operations.
Normal Wayland clients receive assigned mobile window bounds, rather than
implementing arbitrary interactive move/resize. Returning through recent tasks
also restores the application's broker foreground priority.
The shell dispatches background transitions at navigation time, rather than
during rendering, and retries a failed background request on refresh. Home,
all-apps, recent tasks and both manual/idle lock use this path; opening/closing
the shade alone preserves the app's foreground priority. Recent-task dismissal
uses asynchronous `close` and shows an inactive closing row until exit is
observed. Activating another task retains that app's PID and selects it as the
only foreground app. An externally closed active app returns the shell home.

## Notification daemon

`notificationd` owns a separate in-memory notification queue. Its endpoint is
`/run/leon/notifications.sock`, an AF_UNIX stream socket with mode 0666. It uses
one newline-terminated JSON object per connection, at most 8191 bytes before
the newline, and replies with newline-terminated JSON followed by EOF. Every
reply includes `ok`; failures also include `error`. Partial reads/writes are
supported, with sixteen concurrent connections and a one-second absolute
transport deadline for each request and response phase. Local peer credentials
determine authorization; request fields cannot override an ordinary application's UID.

| Operation | Fields | Caller |
| --- | --- | --- |
| `notify` | `title`, at most 160 UTF-8 bytes | installed application UID |
| `notifications` | none | root or desktop UID 1000 |
| `notify_for` | `uid`, `title` | root only; reserved for the legacy broker relay |

The service resolves both built-in and installed application identities through
the same validated catalog as the launcher, without calling the launcher. An
uninstalled application's reserved UID is insufficient to submit a notification.
Application-supplied `app` and `uid` fields in `notify` do not select its source.
The queue retains the latest 64 entries, evicting the oldest first. Listing
returns `notifications: [{"app": "APP_ID", "title": "TEXT"}, ...]` in oldest-first
order. Submission returns `{"ok":true}`. Notifications contain no body, action,
timestamp or persistent ID. They are not persisted, and restarting notificationd
clears the queue. This protocol adds no clear/update operations, channels,
permissions UI, sound/vibration or network push service.

The root daemon accepts `--root DIR`, `--builtin DIR` and `--socket PATH` for
isolated development. Its C17 entry point, concurrent socket loop and bounded
store use a private C++ JSON/catalog adapter to reuse the existing manifest and
identity policy; no new third-party JSON parser or public SDK is introduced.

Buildroot installs the daemon through `leon-core`. The supervised service is
named `notifications`, starts after `prepare`, and restarts on exit. The shell
starts after it and polls the endpoint approximately once per second. It clears
stale rows and shows an unavailable state on connection failure, then recovers
on the next successful poll. Application/settings refresh remains independent
of notification availability. The application broker and notification daemon
do not depend on one another's service lifecycle; either can be restarted
without restarting the other. Notification storage remains volatile.
`leonctl status notifications` reports the service; restarting it requires root
and clears its in-memory queue.

## LMUI

Applications include `<lmui/lmui.hpp>` and link through `pkg-config lmui` with C++20. `Application` owns the Wayland connection, window, input processing and event loop. Applications supply `paint`, `pointer`, `scroll`, `key` and `tick` callbacks. `redraw()` schedules a fresh frame; `quit()` ends the loop. A maximum of three live shared-memory buffers bounds frame allocation.

The umbrella header remains available. Public interfaces may also be included
individually as `<lmui/types.hpp>`, `<lmui/canvas.hpp>`, `<lmui/widgets.hpp>` and
`<lmui/application.hpp>`. Wayland connection, buffer, rendering and input
implementation headers are private to the shared library and are not SDK APIs.

CMake consumers can use `find_package(LMUI 1 CONFIG REQUIRED)` and link
`LMUI::LMUI`; this target declares C++20 and the public Cairo include/link
requirements. `pkg-config lmui` remains supported. LMUI's SONAME and API
generation remain 1. `Development` adds SDK files to a `Runtime` installation;
internal package, IPC and built-in application-support headers are not
installed as public SDK interfaces.

The motion additions retain the original `Application`, `Canvas` and `Widget`
object layouts and existing entry points, including the four-argument Canvas
constructor. Per-application motion and input state live in the private backend;
Canvas borrows a motion context through its Cairo context without adding object
storage. Existing SDK binaries can continue using the generation 1 library.

`Canvas` provides shapes, clipped drawing, Pango text and image rendering. `Widget` provides retained component trees, rows/columns, labels, buttons, fields, toggles, sliders, layout and hit testing. Application callbacks own state. Components do not implement a declarative language or bind arbitrary borrowed application objects.

The compatibility `on_pointer_motion(callback)` method reports pointer and
primary touch movement without repeating down/up actions. New clients can use
`on_pointer_event` for a single captured path including cancellation.

`motion.hpp` and `input.hpp` add Material motion tokens, interruptible animations,
stable keyed canvas animation, content transitions and captured touch input.
`Application::on_pointer_event` reports down/move/up/cancel phases. `TouchInput`
provides release activation, drag tracking and clipped visual ripples; the
older pointer callback remains available. Surface frame callbacks pace active
motion and repainting stops when it settles. `Application::reduce_motion(true)` or
`LMUI_REDUCE_MOTION=1` disables spatial motion while retaining static held-state
feedback. See [motion.md](motion.md) for timing, lifetime and SDK examples.

The reference SDK example is `examples/hello`. Debug-only environment conventions `LMUI_CAPTURE_FRAME` and `LMUI_QUIT_AFTER_FRAME` capture a settled rendered PNG and terminate after the first frame, respectively; normal launch sanitizes inherited environment variables.

For independent shell rendering, `LEON_SHELL_CAPTURE_SHADE=compact` or `expanded`
selects a settled shade stage only when `LMUI_CAPTURE_FRAME` is also set. This
capture convention does not open the shade during normal startup.

### Generation 1 system service transport

The private C service runtime in `src/services/common` hosts one JSON object
request followed by `\n` per Unix stream connection. Replies contain `ok` and,
on failure, `error`; they end with a newline and EOF. Requests are at most
8,191 bytes, have at most 32 levels of JSON nesting and cannot contain embedded
NULs. Sixteen concurrent clients have absolute one-second read/write deadlines;
partial I/O and clients that stop reading cannot hold the event loop indefinitely.
The server checks `SO_PEERCRED` and never treats client-supplied identity as the
peer. Root and desktop UID 1000 are trusted; each operation specifies any other
allowed identities. A stale socket is replaced only after confirming no live
listener owns it. Readiness and watchdog packets use leond's private channel.

All policy replacements fsync a private temporary file, rename it and fsync the
parent directory. Helpers have bounded waits, reset inherited signal masks and
are killed when their service parent dies. `--test` plus temporary state/proc/sys
roots permits protocol tests without changing the host's mounts, clock, network,
core pattern or power state. The C++ JSON/package adapter is internal; domain and
transport code uses C17. This transport is separate from the existing app and
notification protocols.

The generation-1 memory, power, storage, network, time, crash and permission
operations, authorization rules and recovery limits are specified in
[core-services.md](core-services.md). Service names in that document are the
actual leond units; `lmsys` talks directly to each independent daemon.

Additional settings, application interaction, document and audio service
protocols are specified in [platform-services.md](platform-services.md).

### LMUI text input and external event descriptors

`Application::text_input_available()` detects text-input-v3. A client enables it
with `text_input(true)` while an editable field is selected, disables it when
leaving that field, and registers `on_text_input(commit, preedit, erase)`. Commit
and preedit are UTF-8; erase lengths are bytes before/after the cursor as defined
by Wayland. Events are applied together on a matching `done` serial and only to
the entered/enabled surface. Physical keys still arrive through `key`; clients
must avoid committing them twice if they implement an IME keyboard grab.
Built-ins preserve their local keyboard fallback on compositors without v3.
Target applications reserve space through compositor-owned window resizing.
The protocol state and callbacks live in the private implementation, preserving
LMUI generation-1 public object layout.

`text_input(enabled, private_input)` retains the original one-argument overload
and marks password fields with text-input-v3 `hidden_text`, `sensitive_data` and
the password content purpose. The input method then commits letters directly
instead of composing pinyin/candidates. Clients still own masking, clearing
credential buffers and excluding secrets from saved state. The internal mobile
window helper exposes `private_text_input()` for this purpose; Settings overrides
it while an authentication dialog is active.

`watch_fd(fd, callback)` integrates up to 16 caller-owned descriptors into the
LMUI event loop; `unwatch_fd(fd)` removes one. Readable, hangup and error events
invoke the callback on the UI thread. Callbacks must use bounded/nonblocking I/O
and remove their watch before closing the FD. Replacing a watch carries a new
generation so an old poll result cannot dispatch to a reused descriptor. This is
used by the input method's separate Wayland protocol connection, avoiding
200-millisecond keyboard dispatch delays. The library never closes watched FDs.

`watch_fd(fd, interests, callback)` accepts `fd_readable`, `fd_writable` or both.
Writable watches must be removed when a queue drains to avoid a busy loop.
`key_modifiers()` reports `key_shift`, `key_alt`, `key_control`, and `key_super`
for the current key callback without changing the generation-1 `KeyEvent` layout.
Keyboard repeat follows the compositor's rate/delay and stops on release or
focus loss. `focused()` reports Wayland keyboard focus.

`copy_text(UTF8)` offers a focused client's text through the standard Wayland
clipboard using its latest input serial; it returns false when unavailable or
over 1 MiB. `request_clipboard(callback)` asynchronously delivers UTF-8 text
(empty on unavailable/failed transfer). Transfers are nonblocking, capped at
1 MiB and three seconds, with at most four simultaneous transfers. These
internal transfers share the descriptor-watch limit. Copy ownership ends when
another source replaces it or the app exits; there is no clipboard history.
LMUI does not accept terminal escape sequences as clipboard requests.

### Device authentication and compositor session control

The separate `authentication` service, privileged roles, newline JSON protocol,
one-use grants, durable rate limits and compositor lock handshake are documented
in [device-authentication.md](device-authentication.md). Ordinary applications
cannot invoke credential operations or become the trusted shell/IME by choosing
an app ID. `powerd`'s legacy `locked` report no longer sets production lock state.
The shell must submit an opaque page and acknowledge the current lock epoch only
after LMUI confirms that Wayland processed its buffer commit.

The `root.exec` manifest extension, rootd `app_*` single-command protocol,
installer instance binding and `LeonRoot::Root` C++20 SDK are specified in
`root-access.md` and `application-format.md`. Root is independent of the ordinary
portal permission protocol; it has no persistent allow grant. `libleon-root.so.1`
exports the application request API; password/control adapters remain internal
to the installed system UI. Buildroot's `leon-core` installs this SDK to staging.
