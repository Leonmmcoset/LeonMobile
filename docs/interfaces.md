# Version 1 interfaces

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

`/run/leon/services.sock` accepts one newline-terminated text command per connection. `status [name]` returns tab-separated service state, PID, exit code and retry count. `start name`, `stop name`, `restart name`, `poweroff`, and `reboot` require root and return `OK` or `ERR ...`. The client is `leonctl`. No network listener is exposed. This is a local v1 control interface, not a stable remote API.

## Application broker

`/run/leon/apps.sock` accepts one JSON object followed by a newline, up to 8191 bytes. Responses are newline-terminated JSON with `ok`; failures include `error`. Transport callers must handle partial reads/writes and wait for EOF. Credentials come from the socket peer, never request fields.

| Operation | Fields | Caller |
| --- | --- | --- |
| `list` | none | root/desktop |
| `launch`, `stop` | `id` | root/desktop |
| `install` | absolute `file` path | root/desktop |
| `remove` | `id`, optional `purge` | root/desktop |
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

Settings are `dark` (boolean), `reduce_motion` (boolean), `language` (`zh-CN` or `en`), `navigation` (`gestures` or `buttons`), and `volume` (integer 0–100). Volume changes require a live PipeWire sink; the helper executes as the desktop user with a bounded timeout. Defaults are light theme, full motion, simplified Chinese, gestures and a preferred volume of 70. Existing stores without `reduce_motion` read as false. The stored volume represents the user's last requested volume.

The trusted shell controls focus through `/run/leon/wm.sock`, mode 0600, using `shell` or `focus APP_ID`. The compositor also checks peer UID. Ordinary application users cannot invoke these operations. Normal Wayland clients receive assigned mobile window bounds, rather than implementing arbitrary interactive move/resize.

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
