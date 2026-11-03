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

`type` is `simple` (default) or `oneshot`. A oneshot dependency is satisfied only after successful completion; a simple dependency must have a running process. A failed oneshot blocks dependents. Command execution uses an argument vector, without implicit shell interpretation. Explicit `/bin/sh -c` is possible for reviewed system service definitions.

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

Settings are `dark` (boolean), `language` (`zh-CN` or `en`), `navigation` (`gestures` or `buttons`), and `volume` (integer 0–100). Volume changes require a live PipeWire sink; the helper executes as the desktop user with a bounded timeout. Defaults are light theme, simplified Chinese, gestures and a preferred volume of 70. The stored volume represents the user's last requested volume.

The trusted shell controls focus through `/run/leon/wm.sock`, mode 0600, using `shell` or `focus APP_ID`. The compositor also checks peer UID. Ordinary application users cannot invoke these operations. Normal Wayland clients receive assigned mobile window bounds, rather than implementing arbitrary interactive move/resize.

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

`Canvas` provides shapes, clipped drawing, Pango text and image rendering. `Widget` provides retained component trees, rows/columns, labels, buttons, fields, toggles, sliders, layout and hit testing. Application callbacks own state. Components do not implement a declarative language or bind arbitrary borrowed application objects.

The reference SDK example is `examples/hello`. Debug-only environment conventions `LMUI_CAPTURE_FRAME` and `LMUI_QUIT_AFTER_FRAME` capture a rendered PNG and terminate after the first frame, respectively; normal launch sanitizes inherited environment variables.
