# LeonMobile 1.0.0 validation

The developer prototype was built and tested on 2026-11-03 and 2026-11-04 using an x86_64 Linux host and QEMU software emulation of AArch64. This record describes observed behavior; it does not certify a physical phone or a production release.

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

## Independent notification daemon validation (2026-11-04)

The notification split passed six native CTest checks and sixteen Python
integration checks. Real UID tests cover direct submissions, desktop-only
listing, forged source fields, the root-only legacy relay, installed/removal
identity changes, UTF-8 byte limits, 64-entry eviction, fragmented/malformed
requests (including invalid UTF-8 and raw NUL), slow concurrent clients and
crash/socket recovery. Restarting the application broker preserved the separate
notification queue; stopping notificationd left application listing and settings
available, and restarting notificationd cleared its volatile queue as specified.
The staged Runtime check verified the separate daemon ELF. A system-only native
build also succeeded with graphics and tools disabled.

Independent headless Weston rendered all eight apps, the shell and both shade
stages. Material motion checks observed 32 intermediate frames, no settled
repaints, and two requested final frames with reduced motion. The four Buildroot
source units were rebuilt, with the final notification service rebuilt after
protocol validation, and the complete default image was regenerated. The new
daemon is AArch64 ELF with `/lib/ld-musl-aarch64.so.1` as its interpreter.

The full QEMU test passed against `output/images/` with the notification probe
and the existing generation 1 SDK example package. Service status and process
identity confirmed notificationd running independently of the application
broker. The terminal UID submitted eleven notifications directly and one via
the compatibility relay; ordinary app listing was rejected, all twelve sources
remained the terminal despite forged request identity fields, and the desktop
observed the same queue through both endpoints. Notification drag scrolling
and tapping to open the owning app passed, along with the existing calculator,
shade controls/focus, settings/audio, all built-in identities, SDK
install/launch/remove, networking and clean UI poweroff checks.

Evidence remains ignored in `build/notificationd-*.log`,
`build/qemu-test/notifications-service.txt`, `notifications.json`, serial logs
and shade display captures. The daemon remains a volatile local notification
service for the QEMU developer prototype; persistence, clear/update operations,
channels, alert hardware and network push are outside this change. The local
`dist/` release bundle was not regenerated.

## Recorded system tour (2026-11-04)

`scripts/record-qemu.py` completed a real-time boot, touch-driven tour of all
eight built-ins and UI poweroff using the default image on a private disk copy.
The native framebuffer and Xvfb viewport matched at 480×960. The resulting
silent MP4 contains 4,684 H.264/yuv420p frames at 30 fps, runs 156.134 seconds
and includes twelve playback chapters. Full FFmpeg decoding completed without
errors, and decoded frames were inspected for the lock screen, calculation,
settings, stopwatch, keyboard, live notification, image and terminal.

The video, preview, chapter timeline and serial/encoder logs are ignored under
`build/recordings/LeonMobile-1.0.0-boot-and-apps*`; the capture command and host
requirements are documented in [building.md](building.md#record-a-system-tour).
The recording includes the actual graphical startup wait and application
animations, with no playback acceleration or audio track.

## Core platform and shared input method (2026-11-04)

The default AArch64/musl distribution now integrates lifecycle/checkpoint
coordination, authenticated service readiness and watchdogs, seven independent
system daemons, kernel application resource/isolation policy and `imed`.
Eight CTest checks and 33 Python integration checks passed. They include real
Unix UID boundaries, durable policies, expired inhibitors, malformed/slow peers,
stale sockets, permission decisions and traversal/symlink denial, bounded crash
retention, exit history and service/app timeout escalation. The extended portal
fixture also confirmed that a pending file picker cannot substitute for a new
permission confirmation after revocation.

Independent Weston rendered all eight applications, shell and both shade
stages. Its motion fixture observed intermediate frames, no settled repainting
and only two frames with reduced motion. Separate clients acknowledged every
lifecycle callback and dispatched external FD readiness on the UI loop.

The final default-image QEMU run included both test probes and the existing
generation-1 SDK example package. The root system probe reported eight passed
stages:

| Stage | Observed target behavior |
| --- | --- |
| Memory | Real allocation pressure reclaimed a background app while retaining the foreground app |
| Power | Idle lock requests, expiring inhibitors and peer authorization |
| Storage | Real USB ext4 mount, read-only write denial, busy unmount failure and recovery |
| Network | Interface disable/enable, DHCP address/DNS renewal and recovery after killing udhcpc |
| Permissions | Confirmation, selected-file private copy, traversal denial and grant revocation |
| Time | Timezone switching, RTC write/read and a correlated local UDP SNTP exchange |
| Process cleanup | Detached app cgroup members killed on stop; observed exit records retained |
| Isolation/crash | Landlock/seccomp denials, allowed network grant, real cgroup PID/OOM limits and kernel fatal-core capture |

The full interactive sequence also passed twelve live notifications, both
shade stages and input restoration, returning through recent tasks with correct
foreground priority, settings/theme/language/navigation, actual PipeWire sink
volume, every built-in ELF/UID, SDK installation/launch/removal, locking and
clean UI poweroff. New input checks typed `nihao`, selected `你好`, deleted the
last UTF-8 character and restored it, killed/restarted imed while the field was
enabled, then denied and approved file import through the trusted shell picker.
After shutdown, offline ext4 inspection verified the exact saved UTF-8 note
`你好` and the selected document text `LeonMobile 中文\n` in the editor's private
saved state.

The final protocol-completeness pass added `lmpkg save APP_ID` to dispatch the
existing save callback. All eight CTest and 33 Python checks passed again,
including successful checkpoint acknowledgements, missing/legacy/untrusted
request rejection and an unresponsive save timeout. After rebuilding leon-core
and regenerating the default image, the complete QEMU command passed again.
The editor acknowledged the explicit save while retaining foreground state and
PID; the offline note/import assertions and clean UI shutdown also passed.
Memory-probe assertions check the actual foreground/background PIDs after
reclaim, rather than relying on installed-manifest names. Evidence for this
last run is `build/core-suite-checkpoint-{native,target,image,probes,qemu}.log`.

Readiness checks wait for modal-specific controls and settled panel handles;
editor backgrounds and transient client frames cannot pass those checks.
Permission-dialog input is restricted in the compositor as well as the shell.
The status bar reads netd/powerd state and displays unknown supply information
when the guest exposes no power supply.

Asynchronous task dismissal added four native identity tests. All eight CTest
checks and 37 Python integration tests passed, as did the independent Weston
smoke. The rebuilt AArch64/musl image passed the core-only QEMU suite with an
actual SIGSTOP-frozen Wayland app: close returned before checkpoint grace elapsed,
another app launched and retained foreground priority, repeated closes did not
reset deadlines, SIGTERM escalated to SIGKILL, and detached cgroup members died.
Native checks additionally covered concurrent closes, acknowledgement without
exit, cooperative checkpoints, legacy termination, access denial and exact exit
history. Evidence is `build/task-lifecycle-step1-{native,wayland,target,image,qemu}.log`.

The shell integration passed the complete QEMU command with both probes and
the SDK package after rebuilding leon-desktop and regenerating the image.
Interaction checks now observe lifecycle acknowledgements for home, all-apps,
recent-task activation and manual lock/unlock, retain the PID when resuming,
and read target `oom_score_adj` (0 foreground, 500 background). Dismissing the
calculator produced one status-0 exit; reopening created a different PID and
restored the exact rendered calculation from its checkpoint. Shade interactions
preserved foreground priority. The full notification, settings, service-fault,
IME, permission, package, offline ext4 and clean UI poweroff checks also passed.
All eight CTest and 37 Python checks, plus the independent Weston smoke, passed
for the integrated shell. Evidence is
`build/task-lifecycle-step2-{native,wayland,target,image,qemu}.log` and
`build/qemu-test/lifecycle-*.{ppm,json}`.

Reproduce with `make test`, `./scripts/test-wayland.sh`,
`./scripts/build-target-probes.sh` and the full target command in
[building.md](building.md#target-validation). CI's optional image job includes
both probes. Evidence remains ignored under `build/core-suite-*.log`,
`build/screenshots/` and `build/qemu-test/`, including `system-services.txt`,
`ime-*.ppm`, `permission-*.ppm` and serial output. The default image was rebuilt;
the previously assembled `dist/` bundle was not regenerated.

These checks establish behavior on the reference emulator. Physical suspend,
wake sources, battery gauges and phone peripherals require hardware validation.
The pinyin engine is a bounded basic candidate engine, and the network manager
currently manages one Ethernet interface. Exact supported operations and limits
remain specified in [core-services.md](core-services.md).

## Interactive terminal rewrite validation (2026-11-04)

The terminal now separates VT/history, PTY session, input translation, fixed-cell
rendering and mobile controls. Ten native CTest checks and 37 Python integration
checks passed. The two terminal CTest checks also passed with explicit assertion
flags, which keep assertions active in Release builds too. The VT/PTY probe covers SGR attributes,
indexed/RGB colors and blank-cell backgrounds, cursor/erase/insert/delete,
alternate screen, split UTF-8, CJK/combining/emoji cells, DEC line drawing,
cursor/status replies, modified/application/function keys, bracketed paste,
mouse/focus reports and bounded scrollback with a width change. A real controlling
PTY transferred a hash-checked 512 KiB payload under backpressure, rejected an
oversized queued write, delivered resized dimensions, closed inherited FDs and
kept the shell usable after interrupting a foreground task with Ctrl-C.

Independent headless Weston rendered all eight apps, the shell and shade stages;
lifecycle, nonblocking I/O and motion checks passed. The independent X11 Weston
seat on Xvfb passed actual physical modifiers, key repeat and a 256 KiB UTF-8
clipboard transfer. Its terminal sequence executed shell commands, interrupted
sleep, saved a file through vi, selected/copied/pasted exact text and restarted
an exited shell in the same application.

The AArch64/musl app, BusyBox tools, DejaVu monospace fonts and terminfo were
rebuilt into the default image. The complete QEMU command in [building.md](building.md)
passed with system, notification and terminal probes and the SDK package.
The terminal probe ran through the sandboxed app's PTY under UID 11005. Real
compositor input exercised RGB blank backgrounds, Ctrl-C, BusyBox vi and top,
selection/clipboard and exit/restart. After clean UI poweroff, offline ext4
inspection verified both probe success records and the exact interruption marker,
vi file (`LeonMobile terminal\n`), pasted text (`CLIP_TEXT`) and restarted-shell
marker. The existing service fault/isolation, lifecycle, 12 notifications,
settings/audio, app identities, SDK installation/removal, IME, file permission,
saved editor data and clean compositor exit checks also passed.

The combined sequence waits for completed notification submissions, distinguishes
the task page from a calculator frame and waits for settled sound-page geometry
and a newly persisted volume before checking PipeWire. These observations replace
timing assumptions exposed by the longer terminal interaction sequence.

Evidence remains ignored in `build/terminal-lmui-*.log`,
`build/terminal-rewrite-*.log`, `build/screenshots/` and
`build/qemu-test/terminal-*.{ppm,png}` with the final serial/audio/network logs.
The default `output/images/` image was regenerated; the older assembled `dist/`
bundle was not regenerated. Compatibility follows the libvterm subset described
in [terminal.md](terminal.md), with QEMU developer-prototype scope.

## Developer root policy and Settings

The image explicitly disables conventional root password login and BusyBox
su/sulogin, retains a locked shadow entry during incremental image finalization,
and rejects sudo/sudoedit with status 126. The separate non-setuid `leonsu`
client delegates authenticated elevation to rootd. `make test` passed all ten
CTest checks and 50 Python tests. Twelve root tests use private stores/proc
fixtures, real Unix peer credentials and controlling PTYs; they cover password
storage/modes, durable cooldown, malformed/unsafe policy, executable/UID
authorization, rotation/revocation, exact terminal size/exit status, echo and
terminal restoration, service override recovery, path traversal/symlink denial,
slow clients/ancillary rejection and six-session capacity with reserved control
workers. A separate tools-only build also passed.

Independent headless Weston rendered all eight applications, shell and shade
stages and passed lifecycle, motion and event-I/O checks. The Weston input suite
passed modifiers/repeat/256 KiB clipboard, terminal shell/vi/copy/restart, and the
Settings developer flow: first password setup, fixture sysctl editing,
disable/re-enable and password rotation. Passwords are masked, excluded from
saved state, cleared on dialog completion/cancellation/focus loss and marked
private through the additive LMUI text-input overload.

The focused AArch64/musl QEMU sequence passed default-deny/executable identity,
durable authentication, actual `/proc/sys/kernel/hostname` write/restore,
bridge module load/unload, service control/configuration, root PTY credentials,
and cgroup revocation of detached descendants. A target capacity probe filled
all six permitted root sessions and verified a seventh was rejected while
status and authenticated disable still succeeded. Real compositor input
exercised the visible System → Developer options entry, the Chinese IME's private
password path, root disable/re-enable, kernel/module/core pages, notificationd
restart and retry-limit persistence. Two `su -c` requests from terminal UID 11005
with `NoNewPrivs:1` each required a password, obtained UID 0, read root-private
policy and created root-owned proof files; sudo still returned 126. Two actual
guest reboots verified enabled/disabled state, rotated password retention and
service boot/restart overrides, including an autostart-disabled service.

The final image passed the combined QEMU run with system, root, notification and
terminal probes plus the existing SDK package. This also verified service fault
recovery, permissions/IME, app lifecycles, quick-settings gestures, persistent
settings, network/audio, package installation/launch/removal, offline editor and
terminal data, and clean compositor shutdown through the UI. The default
`output/images/` image was regenerated; the older assembled `dist/` bundle was
not regenerated.

Reproduction is documented in [root-access.md](root-access.md). Evidence stays
ignored in `build/root-developer-*.log`, `build/rootd-native-tests.log`,
`build/root-tools-only.log`, `build/screenshots/developer-*` and
`build/qemu-test/{root-phase-*,root-ui-proof.txt,developer-*}`. These observations
apply to the QEMU developer prototype. Kernel image replacement, production
device support and security against a deliberately unrestricted root user are
outside this validation.

## Device authentication and protected screen locking

The authd integration was validated on the AArch64/musl QEMU reference platform.
`make test` passed all 10 CTest checks and 60 Python integration tests. The
independent Weston smoke, commit-barrier ordering, transition privacy reset,
physical key/clipboard, terminal, developer Settings and device credential input
checks also passed. Native authentication fixtures cover production peer
impersonation, PIN/password setup/rotation/removal, persistent retry delays,
malformed policies/transport, grant expiry/replay/audience/PID/epoch binding,
monitor responsiveness, and cancellation of credential changes when a lock
arrives during derivation.

The focused QEMU `--auth-only` suite passed real Settings PIN setup, masked input,
touch keypad unlock, private IME password unlock, authenticated credential
removal, manual/idle locking and independent root/device passwords. It verified
that private app scenes are hidden and input focus is cleared, a held pointer
grab cannot retain the app, bare unlock and compositor shortcuts cannot bypass
credentials, and same-UID forged shell/IME clients receive neither focus nor key
events. It stopped and crashed authd and the shell, checked the compositor's
opaque cover and locked recovery, then verified the fifth-failure cooldown
survived another authd crash and a real kernel reboot. The configured device PIN
still unlocked while the separate root policy was disabled. Input during a
cooldown is discarded, so it cannot accumulate into the next attempt.

The combined QEMU run passed the authentication and root protocol probes, core
service/sandbox/cgroup/crash probes, native terminal VT/PTY tests, live
notifications, Chinese IME input and restart, permission/file portal flows, all
eight built-ins, SDK example installation, shade/navigation/lifecycle controls,
and clean UI poweroff. Offline ext4 inspection verified the exact UTF-8 editor
state and terminal edits/clipboard results. Test service fault probes run before
the shell's initial lock so their intentional foreground transitions do not race
the actual boot lock. These are disposable-image test ordering changes, not
production service dependencies.

Evidence remains ignored in `build/auth-final-native.log`,
`build/auth-final-daemon-tests.log`, `build/auth-final-wayland.log`,
`build/auth-final-settings-wayland.log`, `build/auth-*-regression.log`,
`build/auth-ui-qemu.log`, `build/auth-final-full-qemu.log` and
`build/qemu-test/`. The Buildroot image under `output/images/` was regenerated;
the older `dist/` release bundle was not regenerated. The production image contains
no test helpers or fixture credentials. Reproduction and precise trust boundaries
are in [device-authentication.md](device-authentication.md). Physical phone
hardware and an independent security audit remain outside this validation.
