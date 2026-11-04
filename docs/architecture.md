# LeonMobile 1.0.0 architecture

LeonMobile is a developer graphics prototype for QEMU ARM64, structured as a standalone distribution. It is not yet a production phone release. The reference platform has no modem, camera, battery, Wi-Fi or Bluetooth support. The status bar reports external power rather than a simulated battery percentage.

## Layers and ownership

| Layer | Implementation |
| --- | --- |
| Kernel | Linux 7.3.1, ARM64 QEMU configuration and LeonMobile fragment |
| Toolchain | Buildroot-pinned Bootlin AArch64 musl GCC 14 toolchain |
| Build integration | Buildroot 2026.08.1 and the `distro` BR2_EXTERNAL tree |
| PID 1 and services | C17 `leon-init`, `leond`, `leonctl` |
| Graphics | wlroots 0.20 C adapter and C++20 LeonMobile shell |
| UI | C++20 LMUI 1, Cairo/Pango, shared-memory Wayland buffers |
| Applications | Native AArch64 ELF executables, built-in manifests and local `.lmpkg` archives |
| Network/audio | BusyBox DHCP, iproute2, PipeWire, WirePlumber, ALSA |

The compositor is derived from the CC0 tinywl reference implementation, then adapted for fixed mobile window geometry, a privileged shell overlay, pointer and single-contact touch input. The trusted C++ shell runs as a separate Wayland client to isolate shell failures from the wlroots event loop. LMUI uses the same theme and rendering code for shell and applications. wlroots remains a C integration boundary rather than exposing unstable C headers to C++ applications.

Each built-in application owns a CMake target, manifest, entry point, state and
view under `src/apps/NAME/`. Calculation, document I/O, directory operations,
image decoding, terminal emulation and stopwatch state belong to their respective
application, rather than a shared mode dispatcher. Only the terminal links
libvterm; only the image viewer links libjpeg. Internal `leon-app-support` owns
the common window chrome, preferences, hit handling and on-screen keyboard.
The `leon-ipc` client library implements the local JSON transport without linking
the privileged package installer, OpenSSL or libarchive into applications.
Native build executables live in `build/host/bin/`, independent of source paths.

## Source ownership

| Directory | Responsibility |
| --- | --- |
| `src/libraries/lmui` | Public UI headers and shared LMUI runtime |
| `src/libraries/ipc` | Unprivileged local JSON client transport |
| `src/libraries/app-support` | Internal built-in window, input and UI helpers |
| `src/libraries/package` | Manifest policy, durable JSON, catalog, archive verification and installation transactions |
| `src/services/init` | Early root setup, PID 1 supervision and final shutdown |
| `src/services/serviced` | Service configuration, dependency graph, process lifecycle, scheduling and local control |
| `src/services/appd` | Socket server, peer-authorized broker, application process registry and preferences/audio operations |
| `src/services/notificationd` | Independent notification socket, peer authorization, bounded queue and shared catalog adapter |
| `src/tools` | Service and package command-line clients |
| `src/desktop` | Separate compositor and shell processes |
| `src/apps/NAME` | Application entry point, view, private model/backend and manifest |
| `distro` | Buildroot recipes, board definition, image layout and system policy |

Libraries expose intentional headers under `include/`; implementation helpers
remain private to their component. Applications depend on LMUI and the narrow
client transport. Package policy and archive processing are owned by the tools
and application service. Service configuration and process management do not
depend on graphics. The service manager passes an explicit manager instance to
its graph, process and control modules. The application broker owns separate
process and settings objects; request dispatch does not implement child setup
or audio subprocess handling.

The separate `notificationd` process owns notification state. Its entry point,
nonblocking socket loop and fixed-size store use C17; a private C++ adapter
reuses the existing JSON parser and validated package catalog, including stable
built-in and installed application UIDs. It does not call the application broker
to resolve identities. Shell and built-in notification clients connect directly
to `/run/leon/notifications.sock`; the original application endpoint relays
older clients' verified peer UIDs as root and holds no notification queue.

Each component lists its own sources and dependencies in CMake. Installed
executable paths and the version 1 service protocols do not depend on the
repository layout. Buildroot remains the distribution assembly layer; these
source boundaries do not imply verified production phone support.

The compositor's private modules own output configuration, keyboard input,
pointer routing, single-contact touch, XDG windows, focus and the trusted shell
control socket. Initialization registers listeners per subsystem; shutdown
detaches listeners before destroying scene and input objects. The fixed mobile
window policy has no desktop-style interactive move/resize implementation.
The shell owns session state and navigation separately from status, lock,
home/apps, task, quick-settings and notification views. LMUI separates its public
types, canvas, widgets and application API from its private Wayland backend.

The notification shade is an overlay independent of the underlying desktop or
active app. Its private gesture model follows pointer movement, snaps to compact
or expanded positions and settles using LMUI's Material motion tokens (400 ms
opening, 200 ms closing, immediate with reduced motion). Expanded controls use
the existing peer-authorized settings/audio/power operations. Closing the shade
restores the active app's input region and keyboard focus; the compositor keeps
pressed-pointer motion in its original surface coordinates across input-region
boundaries. Notifications can be scrolled and opened under their known app
identity. Network/power status reflects the reference platform; the panel does
not add radio or brightness hardware support.

## Distribution build units

| Buildroot package | Payload | Principal dependencies |
| --- | --- | --- |
| `leon-core` | PID 1, service manager, application broker, notification daemon and administration tools | JSON, libarchive, OpenSSL |
| `lmui` | Shared LMUI runtime; public SDK in staging | Wayland, Cairo/Pango, xkbcommon |
| `leon-desktop` | Compositor and mobile shell | Installed LMUI SDK, wlroots, JSON |
| `leon-apps` | Eight separate application ELFs and manifests | Installed LMUI SDK, JSON, libvterm, JPEG |
| `leonmobile` | Product dependency selection; no executable payload | All four components |

Each unit uses the shared pinned source/version and keeps its own source list,
dependency declaration, legal metadata and build directory. Runtime installation
contains executable/library/manifest files; SDK installation contains public
headers and CMake/pkg-config metadata. Buildroot assembles the read-only system
and writable data partitions from those units and the board policy. Splitting
build units does not introduce an on-device package repository or OTA service.

## Boot and storage

QEMU directly loads `Image` and the small `leon-initramfs.cpio.gz`. Early `leon-init` mounts proc/sysfs/devtmpfs, waits for the root device, moves early mounts and switches to the root filesystem. The main init mounts `/run`, `/tmp` and `/dev/shm` as tmpfs, mounts `/data`, binds persistent `/data/var` over `/var`, and starts `leond`. The seatd session uses Buildroot's `video` group (GID 28); keyboard layouts come from `xkeyboard-config`.

The reference kernel enables memory PSI, Landlock, seccomp and cgroup v2 memory/process controllers. `prepare` mounts cgroup2, enables controllers beneath `/sys/fs/cgroup/leon/apps` and mounts a private tmpfs at `/media` so removable mount points can be created while the system partition stays read-only. Shutdown unmounts removable volumes before persistent partitions. `/etc/localtime` points into `/run`; the UTC boot default and installed timezone database are available before time policy is applied.

The GPT disk contains `system` (768 MiB ext4, mounted read-only) and `data` (512 MiB ext4, mounted read/write). System software, configuration and built-in apps live in the system image. Installed apps, application-private data, settings and logs live under `/data`. Runtime sockets and device/session state live under `/run`.

`prepare` is a oneshot service that must exit successfully before dependent services start. Other services run in the foreground and are supervised by process group. Notify services hold their dependants until an authenticated READY packet arrives. Ready timeouts, event-loop watchdogs and bounded process-group restarts detect startup failures and stalls; simple services retain process-start ordering.

System updates replace the whole system partition while QEMU is stopped. There is no A/B boot, rollback, online updater or claim of recovery from power loss during system replacement. Local application upgrades use a different transaction mechanism described in the application format document.

## Session and trust boundaries

The desktop session uses UID 1000. Built-ins have stable UIDs 11000–11007; installed apps receive persistent UIDs starting at 12000. App homes and runtime directories are mode 0700, supplementary groups are cleared, and applications run with `no_new_privs`. Applications do not inherit the supervisor's input descriptor. Installed programs and resources are read-only to application users.

The service manager accepts state-changing commands only from UID 0. The application broker identifies callers with `SO_PEERCRED`; the trusted desktop can launch/install/remove apps and request power operations. The settings app can change the supported preferences. Other applications can read preferences. The independent notification daemon also checks `SO_PEERCRED`, accepts bounded notifications under the installed app's own identity, and restricts list access to root/desktop. The broker may relay legacy notification submissions as root with the original peer UID; ordinary clients cannot use that relay operation directly.

Applications now enter Landlock, inherited seccomp socket/syscall policy and cgroup v2 resource limits before exec. The independent permission service owns durable decisions and selected-file copies; the desktop owns confirmation UI. The shared input method uses Wayland text-input-v3 and input-method-v2. All apps still share the Wayland server and AF_UNIX IPC, whose privileged endpoints authorize peers. Full namespaces, authenticated screen unlock, package signatures and a security audit remain outside the developer platform. ELF architecture checks and SHA-256 package checksums provide compatibility and integrity checks, not publisher authentication. See the application format and core service documents for exact enforcement and recovery limits.

The developer serial console logs in automatically as the non-root `leon` user, with no network login service enabled. No device or production security claim is attached to this console policy.

## Version and dependency policy

The distribution target is 1.0.0; development commits are not release certification. LMUI exposes API/SONAME generation 1, manifest format 1 and the v1 service schemas. C++ ABI compatibility across future LMUI releases is not guaranteed. Applications must be rebuilt when the SDK ABI changes.

Buildroot and its package recipes pin dependency versions and hashes. LeonMobile additionally pins the Buildroot archive, Linux archive and external libvterm package. Downloads, build trees and disk images are kept outside version control. `legal-info` preserves third-party license notices and available source archives. Release assembly supplements that output with the Buildroot source archive, musl 1.2.5 and GCC 14.3.0 runtime sources, Bootlin GCC patches, license notices and the toolchain reproduction metadata. The supplementary files are checksum-pinned in `distro/toolchain/legal-sources.json`. The complete external host toolchain is not part of the release. Full disk image byte-for-byte reproducibility has not been certified.
