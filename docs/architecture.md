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

## Boot and storage

QEMU directly loads `Image` and the small `leon-initramfs.cpio.gz`. Early `leon-init` mounts proc/sysfs/devtmpfs, waits for the root device, moves early mounts and switches to the root filesystem. The main init mounts `/run`, `/tmp` and `/dev/shm` as tmpfs, mounts `/data`, binds persistent `/data/var` over `/var`, and starts `leond`. The seatd session uses Buildroot's `video` group (GID 28); keyboard layouts come from `xkeyboard-config`.

The GPT disk contains `system` (768 MiB ext4, mounted read-only) and `data` (512 MiB ext4, mounted read/write). System software, configuration and built-in apps live in the system image. Installed apps, application-private data, settings and logs live under `/data`. Runtime sockets and device/session state live under `/run`.

`prepare` is a oneshot service that must exit successfully before dependent services start. Other services run in the foreground and are supervised by process group. Dependency order controls startup; it does not imply protocol-level readiness for a long-running daemon. Connection retries and bounded process restarts handle readiness races.

System updates replace the whole system partition while QEMU is stopped. There is no A/B boot, rollback, online updater or claim of recovery from power loss during system replacement. Local application upgrades use a different transaction mechanism described in the application format document.

## Session and trust boundaries

The desktop session uses UID 1000. Built-ins have stable UIDs 11000–11007; installed apps receive persistent UIDs starting at 12000. App homes and runtime directories are mode 0700, supplementary groups are cleared, and applications run with `no_new_privs`. Applications do not inherit the supervisor's input descriptor. Installed programs and resources are read-only to application users.

The service manager accepts state-changing commands only from UID 0. The application broker identifies callers with `SO_PEERCRED`; the trusted desktop can launch/install/remove apps and request power operations. The settings app can change the supported preferences. Other applications can read preferences and submit bounded notifications under their own identity.

This is basic Unix identity isolation. It does not include namespaces, seccomp policies, network restrictions, file portals, an IME, authenticated screen unlocking, package signatures or an application permission prompt system. All apps share the Wayland server. Applications are trusted native Linux programs within those stated limits. ELF architecture checks and SHA-256 package checksums provide compatibility and integrity checks, not publisher authentication.

The developer serial console logs in automatically as the non-root `leon` user, with no network login service enabled. No device or production security claim is attached to this console policy.

## Version and dependency policy

The distribution target is 1.0.0; development commits are not release certification. LMUI exposes API/SONAME generation 1, manifest format 1 and the v1 service schemas. C++ ABI compatibility across future LMUI releases is not guaranteed. Applications must be rebuilt when the SDK ABI changes.

Buildroot and its package recipes pin dependency versions and hashes. LeonMobile additionally pins the Buildroot archive, Linux archive and external libvterm package. Downloads, build trees and disk images are kept outside version control. `legal-info` preserves third-party license notices and available source archives. Release assembly supplements that output with the Buildroot source archive, musl 1.2.5 and GCC 14.3.0 runtime sources, Bootlin GCC patches, license notices and the toolchain reproduction metadata. The supplementary files are checksum-pinned in `distro/toolchain/legal-sources.json`. The complete external host toolchain is not part of the release. Full disk image byte-for-byte reproducibility has not been certified.
