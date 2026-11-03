# LeonMobile 1.0.0 validation

The developer prototype was built and tested on 2026-11-03 using an x86_64 Linux host and QEMU software emulation of AArch64. This record describes observed behavior; it does not certify a physical phone or a production release.

| Check | Evidence |
| --- | --- |
| Native core, LMUI and apps | Six CTest checks passed: service configuration/dependencies, widget layout/hit testing/rendering, Material motion/touch/transition behavior, application models, keyboard callback lifetime/input and shade drag/snap/reversal |
| Native integration | Twelve Python checks passed, covering staged runtime/SDK ownership and external SDK compilation, real UID transitions, private app data, broker authorization, package transactions/rejections, dependency-preserving shutdown and GPT/data preservation |
| Independent Wayland peer | All eight independent application executables, the shell and both shade stages rendered under headless Weston; PNGs in `build/screenshots/` |
| Target build | Four independent Buildroot source units (`leon-core`, `lmui`, `leon-desktop`, `leon-apps`) built into the complete image; Linux reports `7.3.1-leonmobile`; target executables are AArch64 ELF with the musl loader |
| Target graphics | Actual DRM/pixman compositor, 480×960 display, Chinese lock/home/app screens and pointer input |
| Target applications | Eight distinct ELF entry points verified; every built-in launched with its expected process name and stable UID; retired dispatcher absent; calculator and settings launched from the desktop; calculator-result frame shows `7 + 3 = 10` |
| Notification shade | Pointer held across status/app boundary followed before release; compact and expanded stages, theme toggle, 25% volume drag, mute/restore, power cancellation, settings shortcut, lock tile, locked top-bar tap/pull and backdrop dismissal passed; original calculator PID, contents and input preserved |
| Live notifications | Test-only AArch64 probe submitted twelve notifications from terminal UID 11005; drag scrolling changed visible rows and tapping a notification opened the same terminal app |
| Settings layout and navigation | Android-style category home and detail pages rendered; search for `volume` opened Sound; network/storage/about pages and nested System pages inspected in QEMU |
| Settings and audio | Dark theme, English language and three-button navigation persisted through their detail controls; Chinese and gestures restored; playback request of 25% matched the real PipeWire default sink volume of `0.25` |
| Network | DHCP address `10.0.2.15`, default gateway `10.0.2.2`, resolver `10.0.2.3`; gateway ICMP succeeded |
| SDK package | Example cross-compiled through both exported CMake and pkg-config interfaces; packaged CMake build transferred with checksum verification, installed, launched as a native ELF app and removed through the broker |
| Shutdown | Quick-settings poweroff stopped the compositor successfully, stopped dependencies in order, unmounted data and powered down QEMU |
| Build/legal metadata | 140-entry software manifest; component license hashes and tinywl notice validated; source archives for each local build unit collected without build/download/image artifacts |

The core-only native build also passed independently of graphics. Native
installation checks use a temporary destination, verify eight independent ELF
manifest entries and compile the external example from the relocated installed
SDK. Target runtime libraries and executables are installed through `Runtime`;
development headers and metadata are supplied through the LMUI staging SDK.

Reproduce the main checks:

```sh
make test
./scripts/test-wayland.sh
make image
./scripts/build-example.sh
python3 scripts/test-qemu.py --package build/hello/org.example.hello-1.0.0.lmpkg
output/host/bin/aarch64-linux-gcc -std=c17 -Wall -Wextra -Os \
  tests/target/notifications.c -o build/notifications-probe
python3 scripts/test-qemu.py --notification-probe build/notifications-probe \
  --package build/hello/org.example.hello-1.0.0.lmpkg
make release
python3 scripts/test-qemu.py --images dist/leonmobile-1.0.0-qemu-aarch64 \
  --package dist/leonmobile-1.0.0-qemu-aarch64/examples/org.example.hello-1.0.0.lmpkg
```

QEMU evidence is written to `build/qemu-test/serial.log`, `network.txt`, `audio.txt` and PPM display frames. The calculator result was also inspected visually. Build and download outputs are excluded from Git; completed implementation steps are committed separately.

Settings evidence includes `settings-light.ppm`, `settings-display-light.ppm`,
`settings-display-dark.ppm`, `settings-dark.ppm`, `settings-search.ppm`,
`settings-sound.ppm`, `settings-network.ppm`, `settings-storage.ppm`,
`settings-system.ppm`, `settings-english.ppm`,
`settings-navigation-buttons.ppm` and `settings-about.ppm`. The category home
also rendered against independent native Weston. Read-only network information
comes from `getifaddrs` and sysfs, storage capacity from `statvfs`, and kernel
version/architecture from `uname`; no radio, battery or permission controls
are implied by the new layout.

Shade evidence includes `shade-dragging.ppm`, `shade-compact.ppm`,
`shade-expanded.ppm`, `shade-expanded-dark.ppm`, `shade-volume-drag.ppm`,
`shade-power.ppm`, `shade-restored-app.ppm`, `shade-restored-input.ppm`,
`shade-notifications-scrolled.ppm`, `shade-notification-open.ppm`,
`shade-locked.ppm`, `shade-locked-top.ppm`, `shade-locked-pull.ppm` and
`shade-backdrop-dismissed.ppm`. The independent Weston check captures compact
and expanded PNGs using the documented capture convention. Gesture checks use
QEMU's absolute USB pointer; physical touch hardware remains unverified.

The eight application executables were all rendered against native Weston and
launched in QEMU; target interaction checks cover calculator, settings and the
SDK example. Host audible playback, external DNS name resolution, microphone
recording, physical touch hardware and other phone peripherals were not
verified. The headless QEMU check uses the `none` host audio backend. It verifies
the guest audio device, graph and volume, without asserting audible sound.
Security, device support and update boundaries are documented in
[architecture.md](architecture.md).

## Material motion validation (2026-11-03)

The integrated LMUI, shell and application changes passed six native CTest checks and
twelve Python integration checks, including a relocated SDK example build.
The independent Weston peer rendered all eight built-ins, the shell and both
notification shade stages.
The motion client observed intermediate frames in normal mode, exact final
values and no further repaints while settled; reduced motion rendered only
the two requested final states. Unit checks cover interruption continuity,
contact cancellation, release activation and continuous slider updates.

An isolated target build used the existing Buildroot AArch64/musl SDK. Its
Runtime payload was staged into a private ext4/disk image copy, leaving the
other development session's Buildroot output untouched. The QEMU target
script passed boot, calculator input, settings pages/search/keyboard, real
PipeWire volume, all eight application identities, networking and clean UI
shutdown. Display → Reduce motion was enabled and disabled through the UI;
both boolean values were checked in the persisted settings store. Evidence
includes `settings-reduced-motion.ppm` and `quick-poweroff.ppm`. An initial
run timed out at shutdown; a rerun with the shutdown-screen capture passed.
This initial private-image check validated the changed clients and settings
service without rebuilding the distribution. The final distribution rebuild
and checks are recorded below; physical touch and vibration hardware remain
unverified.

The shell motion check additionally captured `lock-dragging.ppm` and
`lock-settled.ppm`, verifying that a short drag moves the lock instruction and
returns to its exact original pixels without unlocking. The new cross-built
SDK sample was transferred with checksum verification, installed, launched
and removed in QEMU. Home/app-list navigation, transparent app regions and
shutdown were exercised with frame callbacks and page transitions enabled.
The task dismissal test exposed a pending idle callback on a transparent shell;
frame callbacks are now requested only while another animation frame is needed.
Task dismissal captures cover row reflow and check that the selected process
actually stops.

An additional native client compiled with the pre-motion generation 1 headers
linked and ran against the new library. It exercised Application construction,
callbacks and destruction, the original Canvas constructor and retained Widget
layout/drawing. The public object layouts and original symbols are preserved.

After integrating the independent notification-shade change, the combined
AArch64 runtime and current SDK example passed the full private-image QEMU
check with the notification probe. This included both shade stages, drag
tracking, twelve live notifications, restored application input, volume/mute,
power-menu cancellation, lock protection, backdrop dismissal and UI poweroff.
Snapshot comparisons wait for observed final lock/home/shade geometry so
software emulation does not mistake an entrance frame for a settled reference.

The final main-workspace validation rebuilt all four local Buildroot components
with `./scripts/build.sh leonmobile-rebuild`, regenerated the complete default
image with `./scripts/build.sh` and rebuilt the exported SDK example. Six native
CTest checks, twelve Python integration checks and independent Weston passed
again; the motion fixture observed 31 intermediate frames, no settled repaints
and only two requested final frames with reduced motion.

The full QEMU check then passed against `output/images/` with the notification
probe and an SDK package compiled before the motion changes. The old ELF was
installed, launched, captured and removed without recompilation, validating
generation 1 target compatibility. The current captured-input SDK sample had
already passed the private-image run. Final evidence is in
`build/qemu-test/`, `build/screenshots/` and the ignored
`build/motion-{distribution,image,native,example}-build.log` files. The default
image used by a new `make run` disk includes the integrated motion changes.
