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
| `LEON_SYSTEM` | PID 1, service manager and application broker |
| `LEON_TOOLS` | `leonctl` and `lmpkg` |
| `LEON_GRAPHICS` | Master switch for graphical components |
| `LEON_LMUI` | Build LMUI locally; when off, graphical clients use the installed SDK |
| `LEON_APPS` | Eight independently linked built-ins |
| `LEON_SHELL` | Trusted mobile shell |
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

## Changing an application

Keep its entry point, private state, view, backend/model and `manifest.json`
in `src/apps/NAME/`. Register sources through `leon_add_application` in that
directory's CMake file. The helper installs one named ELF and its manifest;
it never generates a selector executable. Application IDs, stable UIDs and
private data directories must survive source refactors.

Shared UI support belongs in `src/libraries/app-support` only when multiple
built-ins need it. Public UI functionality belongs in LMUI. Ordinary clients
use the IPC transport, while privileged installation and identity policy stay
in `src/libraries/package` and `src/services/appd`.

Run `make test` and the independent Weston check after client changes. Runtime
or image integration changes also require the QEMU test. Native integration
checks stage runtime and development payloads in a temporary directory and
build an external program from the relocated SDK. Target checks launch every
built-in and compare its process command and UID with the installed manifest.
Record observed validation and commit each completed reviewable step before
starting the next. Downloads, build evidence and generated images remain ignored.
