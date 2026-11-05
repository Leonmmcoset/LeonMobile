# Component development

LeonMobile 1.0.0 retains its QEMU AArch64 developer prototype scope. Source and
distribution boundaries are described in [architecture.md](architecture.md).

## Native build selection

CMake uses C17 for system components and C++20 for libraries, the shell and apps.
Each component declares explicit sources; no application mode dispatcher or
source-file inclusion is used. Runtime binaries are emitted to `BUILD/bin`,
shared libraries to `BUILD/lib`. CMake targets carry their dependencies; callers
do not add unrelated component implementation directories to include paths.

| Option | Meaning |
| --- | --- |
| `LEON_SYSTEM` | PID 1, manager, application/notification brokers and independent system daemons |
| `LEON_TOOLS` | `leonctl`, `lmpkg`, `lmsys` and non-setuid `leonsu` |
| `LEON_GRAPHICS` | Master switch for graphical components |
| `LEON_LMUI` | Build LMUI locally; when off, graphical clients use the installed SDK |
| `LEON_APPS` | Eight independently linked built-ins |
| `LEON_SHELL` | Trusted mobile shell and independent input method |
| `LEON_COMPOSITOR` | wlroots compositor |
| `BUILD_TESTING` | Native component checks |

All options default to on. `make test` disables the native compositor because
the reference wlroots version comes from Buildroot. A core-only build is:

```sh
cmake -S . -B build/core -G Ninja -DLEON_GRAPHICS=OFF
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

For an LMUI-only SDK build, disable `LEON_SYSTEM`, `LEON_TOOLS`, `LEON_APPS`,
`LEON_SHELL`, and `LEON_COMPOSITOR`. To build clients against an installed SDK,
set `LEON_LMUI=OFF` and point `CMAKE_PREFIX_PATH` at that installation. The
Buildroot `lmui`, `leon-desktop` and `leon-apps` recipes exercise these paths.

## Installed interfaces

`cmake --install BUILD --component Runtime` installs executables, manifests,
version data and the versioned LMUI shared library. `Development` installs the
public LMUI headers, unversioned link symlink and SDK metadata. Use `DESTDIR`
for staged installation; development and tests never require installing into
the host system. Private headers for the broker, package engine, compositor,
shell and built-in application support are not installed as SDK APIs.

The external reference application uses the exported CMake package:

```cmake
find_package(LMUI 1 CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example PRIVATE LMUI::LMUI)
```

The existing pkg-config interface remains available. Configure
`examples/hello` with `-DLEON_HELLO_PKGCONFIG=ON` to exercise that interface.
`scripts/build-example.sh` uses the cross SDK and packages the resulting ELF.

`LeonPlatform::Platform` (`find_package(LeonPlatform 1 CONFIG REQUIRED)`) exports
the C++20 `<leon/platform.hpp>` client for preferences, network watches, document
capabilities, app interactions and PCM playback. The equivalent pkg-config
module is `leon-platform`; its runtime library is `libleon-platform.so.1`.
See [platform-services.md](platform-services.md) for ownership, limits, retries
and the trusted UI roles. `examples/platform` demonstrates consumption through
either installed SDK interface.

## Changing an application

Keep its entry point, private state, view, backend/model and `manifest.json`
in `src/apps/NAME/`. Register sources through `leon_add_application` in that
directory's CMake file. The helper installs one named ELF and its manifest;
it never generates a selector executable. Application IDs, stable UIDs and
private data directories must survive source refactors.

Core-service ownership, operations and test-mode constraints are specified in
[core-services.md](core-services.md). `imed` keeps its C17 engine and Wayland
backend separate from its C++20 keyboard view. Changes to that protocol bridge,
permission overlays or application priorities require real target interaction;
Weston alone cannot verify the target compositor policy. The complete probe
build and QEMU command are in [building.md](building.md#target-validation).

Shared UI support belongs in `src/libraries/app-support` only when multiple
built-ins need it. Public UI functionality belongs in LMUI. Ordinary clients
use the IPC transport, while privileged installation and identity policy stay
in `src/libraries/package` and `src/services/appd`.

The root credential/FD transport is private to `src/libraries/root-client`.
Settings requests mutations through rootd; its own UID stays unprivileged.
The internal C service/JSON runtime is also available to tools-only builds,
without enabling the system daemons or graphics.

The settings app keeps its page/search catalog, navigation state, preference
views, device views and read-only device queries in separate private modules.
Its Android-style home links to actual supported settings; network and storage
pages report kernel interface data and `statvfs` usage without adding device
control APIs. Built-ins may override the internal window header while retaining
the shared input, preference polling, keyboard and error handling.

The shell's private `Shade` model owns drag thresholds, compact/expanded snap
positions and settling time independently of rendering. Its views own controls,
notification clipping/scrolling and the power menu. LMUI's `on_pointer_event`
callback supplies down/move/up/cancel phases through one captured path; the
older `pointer` and `on_pointer_motion` callbacks remain available. Test
gesture reversal and reduced motion with the native shade check,
then check the shell against Weston and exercise the real compositor/input
boundary with the QEMU target test.

For target notification scroll/activation coverage, compile the test-only probe
with the reference toolchain and pass it to the target check:

```sh
output/host/bin/aarch64-linux-gcc -std=c17 -Wall -Wextra -Os \
  tests/target/notifications.c -o build/notifications-probe
python3 scripts/test-qemu.py --notification-probe build/notifications-probe \
  --package build/hello/org.example.hello-1.0.0.lmpkg
```

The probe runs from the terminal under its established app UID and submits
twelve notifications (eleven directly to notificationd and one through the
legacy broker relay). It also checks that an app cannot list notifications.
The desktop console compares both endpoints' queue contents and source IDs,
and the target check verifies a separate supervised daemon PID. The probe is
never installed in the system image. The target check also verifies shade
drag/snap, theme, volume/mute restoration, app preservation and input recovery, settings launch,
power-menu cancellation, screen locking and final power-off. Device PIN/password authentication and fail-closed compositor locking are
described in [device-authentication.md](device-authentication.md).

Run `make test` and the independent Weston check after client changes. Runtime
or image integration changes also require the QEMU test. Native integration
checks stage runtime and development payloads in a temporary directory and
build an external program from the relocated SDK. Target checks launch every
built-in and compare its process command and UID with the installed manifest.
Record observed validation and commit each completed reviewable step before
starting the next. Downloads, build evidence and generated images remain ignored.
# Surface commit acknowledgement

`lmui::Application::after_commit(std::function<void()>)` schedules a callback on
the UI thread after the Wayland server has processed the next submitted buffer
commit. It uses `wl_display.sync` after `wl_surface.commit`; it confirms server
processing, not physical presentation. A newer call replaces a callback waiting
for its commit. One acknowledgement may be in flight while another waits for the
next commit. Destruction cancels both. The shell uses this barrier before asking
the compositor to reveal a newly painted, opaque lock page. The independent
Weston I/O test verifies ordering, replacement and nested scheduling.

`lmui::ContentTransition::reset()` drops saved page patterns and cancels an active
transition. A security boundary such as entering the lock screen must call it
before painting, so a previous private page cannot appear in the new frame.
