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

## Date/time and notification Settings (2026-11-05)

The native build passes all 12 CTest checks and 106 Python integration checks.
The notification-specific checks cover durable policy, real UID authorization,
strict configuration types, storage failures, disabled-app suppression and
queue removal, direct/legacy lock-screen redaction, authd outage behavior and
audio dispatch. Audio tests check permission boundaries and focus release after
the short system chime. Independent native Weston renders all eight apps, the
shell and both shade stages and passes lifecycle and motion checks.

The updated AArch64/musl image passes the focused preferences QEMU check.
Actual Settings input disables automatic time, searches for `asia/shang`,
selects Asia/Shanghai, switches to 12-hour format, rejects an invalid calendar
date and applies a valid date in that timezone. A local UDP SNTP fixture supplies
a correlated reply to the real timed; its successful synchronization timestamp
and result are displayed and survive service restart and a real kernel reboot.

The notification page controls real global sound, do not disturb, per-app sound
and notification switches. Disabling an app removes its queued notifications
and suppresses later submissions. All three lock-screen policies are exercised
with authd and the compositor: redacted content, visible content, or no cards.
Killing timed and notificationd preserves configuration; notificationd restart
clears its volatile messages. Preferences also survive the actual kernel reboot.

QEMU's WAV output contains the chime's 784/1046 Hz tones for both notification
delivery and preview. Do-not-disturb, global-sound-off and per-app-sound-off
windows contain no audio output. This verifies captured device output; host
speaker playback and physical phones remain unverified.

Reproduce with `scripts/build-target-probes.sh` followed by
`python3 scripts/test-qemu.py --timeout 240 --preferences-probe build/preferences-probe`.
Ignored evidence is in `build/preferences-{native-tests-final,ctest-final,
wayland-interaction-final,qemu-final}.log` and `build/qemu-preferences-test/`:
PNG/PPM captures, serial log, `preferences-proof.json`, `audio-windows.json`,
`audio-proof.json` and `preferences-audio.wav`. The fixed-operation privileged
test helper exists only in the cloned test image. No user disk is modified.

The separate general QEMU regression also passes boot, all eight native app
identities, calculator and lifecycle controls, Settings navigation, actual
PipeWire volume, DHCP/routing/DNS, twelve direct/legacy notifications, scrolling
and activation, lock-screen protection and clean poweroff through the UI.
Its final shutdown step holds a complete USB contact and waits for the rendered
power menu before selecting poweroff; a short down/up pair could previously be
missed under TCG and hit the notification underneath. Evidence is in
`build/preferences-qemu-regression.log` and `build/qemu-test/`, including
`poweroff-menu-ready.ppm` and the serial trace showing clean compositor exit,
data unmount and kernel powerdown.

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

## Settings application management

The native suite passed 11 CTest cases and 66 Python integration tests. The
management fixtures use real Unix peer UIDs and cover protected apps, installed
metadata/source, permission states and reset persistence, shutdown checkpoint
writers, all-version uninstall, identity retention, symlinks/hardlinks/FIFOs,
bounded storage scans and unavailable services. A transport regression checks
that a disconnected service raises an IPC error with default SIGPIPE handling.

Independent Weston passed the eight-app/shell/shade/motion/lifecycle smoke suite.
The dedicated `--applications` input test drove the real Settings UI through
search/details, stopping, cancelling and confirming data clear and uninstall,
permission reset, broker SIGSTOP with responsive back navigation, and a broker
exit/restart followed by Refresh. Screenshots were inspected for the detail,
confirmation and target permission layouts.

The focused AArch64 QEMU service run passed resource-group cleanup of a
SIGSTOP-stalled app and a detached writer before deletion, protected actions,
fresh storage and relaunch, plus the existing core/sandbox/IME/portal checks.
The focused `--applications-only` run drove the same management actions under
the real compositor, rebooted the kernel, verified reset/uninstall persistence,
relaunched the cleared builtin and shut down the compositor cleanly.

The final combined QEMU run also passed authentication/root protocol probes,
core services, cgroup/sandbox/crash handling, terminal VT/PTY/vi/top/clipboard,
live notifications, input method restart, permission/file selection, all builtins,
SDK package install/remove, shade/navigation/lifecycle, the new application
management UI, offline editor/terminal state inspection and clean UI poweroff.
The harness observes page/keyboard/shade readiness and lock-hint settling before
sending dependent input, rather than assuming fixed rendering delays under TCG.

Reproduction and operation semantics are in
[application-management.md](application-management.md). Evidence remains ignored
under `build/app-management-*.log`, `build/screenshots/applications-*` and
`build/qemu-test/applications-*`. The AArch64 image under `output/images/` was
rebuilt; no management test helper is shipped and the older `dist/` release bundle
was not regenerated. Validation is limited to the QEMU developer prototype;
storage counts are allocated-space estimates and interrupted multi-service
cleanup can require retry, as documented in the protocol.

## Third-party Root application authorization (2026-11-05)

The native suite passed all 11 CTest checks and 79 Python integration tests.
Root application fixtures use real Unix peer credentials and cover installed
ELF identity, reserved installation markers, same-version reinstall rotation,
trusted UI impersonation, malformed requests, one-use decisions, cancellation,
policy corruption, persistent block/reset, concurrent capacity and password
failure delays. The C++20 SDK tests cover binary output, nonzero exit status and
pending/running cancellation. A relocated SDK export built the separate app and
helper example successfully.

Independent Weston rendered all eight built-ins, the shell and both shade
stages, with motion and lifecycle checks. The Root-specific input check passed
exact command display, masked private password entry, failed authentication,
fresh confirmation for a second command, deny/block and a stalled service with
responsive UI. Existing developer settings setup, kernel editing, disable,
re-enable and password rotation also passed.

The AArch64/musl production runtime and default image under `output/images/`
were rebuilt. The focused QEMU suite passed real sandboxed app requests and a
separate UID 0 helper, descriptor/environment exclusion, cgroup cleanup of
detached descendants, replay rejection, persistent block/unblock, device-lock
cancellation and rootd crash recovery. Actual application-broker stop, data
clear and uninstall waited for Root writers; reinstalling the same version
retained the app UID while invalidating the old installation request.

The real compositor input sequence passed Root password setup, modal protection
against a home gesture, masked input, bad-password cooldown, fresh command
confirmation, deny/block, authenticated unblocking and authorization history.
A real guest kernel reboot retained the Root switch/password and blocked app
policy. After a successful diagnostic, `/proc/PID/status` and the screen
confirmed the app remained at its own UID with NoNewPrivs and seccomp while the
separate helper reported UID 0 and Linux 7.3.1 on AArch64. Page checks wait for
enabled actions after asynchronous work before sending dependent input.

The final combined QEMU regression also passed the existing authentication and
Root protocol probes, core service fault/sandbox/cgroup/crash checks, terminal
VT/PTY/vi/top/clipboard, live notifications, Chinese IME restart, permission/file
selection, all eight built-ins, SDK package installation/removal, shade and
lifecycle controls, Settings application management and clean UI poweroff.
Offline ext4 inspection verified the exact saved editor and terminal data.

The recorder completed an uncut, silent production-image capture from boot
through password setup, third-party confirmation, failed authentication,
diagnostics, block/unblock and history, followed by clean poweroff. Only the
installable SDK demo was copied into its private disk; no privileged test
service or authentication bypass was injected. The MP4 contains 2,251 H.264
yuv420p frames at 480×960 and 30 fps, lasting 75.034 seconds. Full FFmpeg
decoding completed without errors. Decoded frames were inspected for the lock
screen, exact command, masked password and final app UID 12000/helper UID 0
diagnostic. The movie, PNG captures, timeline and serial/encoder/probe logs stay
ignored under `build/recordings/LeonMobile-third-party-root*`.

Reproduction and trust boundaries are documented in
[root-access.md](root-access.md) and [building.md](building.md). Evidence remains
ignored in `build/third-party-root-*.log` and `build/qemu-root-apps-test/`.
Neither the SDK demo nor privileged regression fixtures are shipped in the
default image. The older `dist/` release bundle was not regenerated.

This validation covers the QEMU developer prototype. Approved unrestricted
Root can modify the system and escape its process controls; revocation cannot
undo completed changes. The feature has not received an independent security
audit or physical phone validation.

## Visible boot console and Wayland handoff (2026-11-05)

The native build passed 11 CTest checks and all 79 Python integration tests.
The AArch64/musl kernel configuration, core runtime, initramfs and default disk
were rebuilt. The complete QEMU regression passed kernel/serial boot, native
authentication and Root probes, core service fault/sandbox/cgroup/crash checks,
terminal VT/PTY/vi/top/clipboard, live notifications, Chinese IME restart,
permission/file selection, all built-ins, SDK package installation/removal,
shade/navigation/lifecycle, Settings application management, offline saved data
inspection and clean UI poweroff. The harness captured a real framebuffer boot
log before Wayland and confirmed both tty0 and ttyAMA0 in `/proc/consoles`.

The focused boot suite used a test-only readiness barrier in a disposable disk.
Actual `/dev/vcs1` cells and the screen contained kernel messages, PID 1 mount
diagnostics and leond service starts in KD_TEXT. The serial login remained UID
1000. After release, the real VT reported KD_GRAPHICS and pixels below the clock
remained identical after genuine `/dev/kmsg` and internal status-mirror writes.
Stopping the compositor restored KD_TEXT, rendered fresh kernel/userspace
diagnostics and allowed a normal restart to the lock screen. SIGKILL also
restored text mode, and supervised compositor recovery used a new PID and
returned to the lock screen. A real kernel reboot repeated text diagnostics,
graphics takeover and successful desktop input.

The relocated release launcher read the shared dual-console command line;
Python compilation, shell syntax and whitespace checks passed. Production boot
contains no readiness barrier or artificial delay. fbcon becomes visible only
after virtio-gpu initialization; earlier available kernel messages are replayed
from its log buffer. The older `dist/` bundle was not regenerated.

Evidence stays ignored in `build/boot-console-*.log`,
`build/qemu-test/boot-console.*` and `build/qemu-boot-console-test/`, including
the VT states, text cells, serial trace and recovery PNGs. These checks apply to
the QEMU developer prototype; physical phone display/VT behavior is unverified.

The standard recorder also completed a fresh production-image tour from boot
through all eight built-ins and UI poweroff, without a privileged boot fixture.
The native framebuffer and 480×960 Xvfb viewport matched. The uncut silent MP4
contains 4,773 H.264/yuv420p video frames at 30 fps, runs 159.100 seconds and has
twelve playback chapters. Full FFmpeg decoding completed without errors.
Decoded frames were inspected for actual kernel messages, init mounts/leond
starts, lock screen, calculator input and the native terminal's kernel version.
The film, `boot-kernel.png`, `boot-services.png`, other captures and
serial/encoder/probe evidence remain ignored under
`build/recordings/LeonMobile-boot-console*`. Source provenance in `recording.json`
points to the validated code commit before this documentation update.

## Unified logging: backend and supervisor integration

The host build and 11 CTest cases pass. Ten real-peer logging integration tests
cover credentials/spoofing, own-versus-system visibility, control/binary escaping,
Settings lock/identity and in-flight revocation, singleton ownership, CLI
write/query/follow and impostor rejection, pagination, restart/previous boot,
torn-tail repair, rotation, quotas, FD injection, stopped/killed writer, unsafe
storage/recovery, finished-service offline buffering, and application output plus
lifecycle handling while logd is down. Evidence: `build/logd-native.log`.

The AArch64 core and probes compile. A QEMU core run booted the real 7.3.1 kernel
and verified kernel/init/service log capture, root/ordinary peer separation,
logd stop/start with retained history and offline output, and actual ENOSPC on a
separate 64 KiB tmpfs. Queries and memory ingestion continued while it was full;
storage recovered after freeing space. The remaining memory/power/storage/network/
time/crash/sandbox/cgroup/management probes and Chinese IME/portal UI checks also
passed, followed by clean poweroff. `build/logd-qemu-core.log` and the QEMU
artifacts contain the results. Settings diagnostics and the final combined image
are validated separately below; this result makes no physical-phone claim.

## Unified logging: Settings diagnostics and final image

Settings now exposes System → System diagnostics, with the actual logd health,
latest records/details, current/previous boot and severity filters, optional live
refresh, leond service state and crashd metadata. Backend, query transport and
the page are separate modules. System data remains restricted to root or the
verified, unlocked Settings process; no Root switch or broad desktop grant is
used. Late tab/filter results are discarded and diagnostic contents are excluded
from saved state, cleared on focus/auth failure, and omitted from retained
pointer callbacks.

The final host build, 11 CTest cases and 89 Python integration tests pass. The
ten logging cases include the new privileged service snapshot and strict filter
types. Independent Weston smoke still renders all eight apps, shell and shades,
and passes lifecycle/motion checks. The independent X11 Weston diagnostics run
uses actual logd, authd, crashd and leond fixtures. It verifies real record/detail
rendering, escaped controls/UTF-8, service/crash tabs, authd stall clearing,
responsive Back when logd disappears, and recovery. Evidence is in
`build/logd-{python-regression,native,ctest,wayland,wayland-diagnostics}.log` and
`build/logging-wayland/`.

The AArch64 core/apps and final ext4/initramfs/disk image rebuild successfully.
The combined QEMU regression passes authentication and Root probes, log
collection/offline recovery/real ENOSPC, core services and isolation, terminal
VT/PTY/vi/top/clipboard, notifications, Chinese IME/portal, all eight built-ins,
package installation/removal, lifecycle/shade controls, app management, saved
data inspection and clean UI poweroff. The focused boot-console regression
also passes actual kernel/init/service text, KD_TEXT/KD_GRAPHICS handoff,
compositor stop/SIGKILL recovery, real reboot and desktop input.

The separate production-image logging run confirms UID 1000 cannot read kernel
logs while the trusted unlocked Settings ELF can, opens full details, reads real
service/crash tabs, changes filters, backgrounds/refocuses the page, protects it
with the real compositor lock, reboots the kernel and reads the prior boot's
journal through Settings, then powers off cleanly. It uses no injected privileged
boot helper. Real 480×960 screenshots, serial trace and `diagnostics-proof.json`
remain in `build/qemu-logging-test/`; main results are in
`build/logd-qemu-diagnostics.log`, `build/logd-qemu-regression.log` and
`build/logd-boot-console.log`.

The third-party Root fixture now queries its own captured records through logd
instead of reading the removed production `application.log`. Its separate QEMU
protocol/UI/reboot suite also passes (`build/logd-root-apps-regression.log`). An
earlier run alongside other emulated guests hit a fail-closed Root request
cancellation; the isolated run passed without relaxing authentication checks or
deadlines. Heavy emulation can still cause authentication requests to be denied
and require retry. These results cover the QEMU developer prototype, not a
lossless audit guarantee, independent security audit or physical phone support.
The older `dist/` release bundle is not regenerated; current artifacts are under
`output/images/` and remain outside version control.

## Platform services and desktop integration (2026-11-05)

The final native build passes 11 CTest cases and all 101 Python integration
tests. Real UID fixtures cover typed settings/schema/defaults, atomic batches,
generation conflicts/watches, migration, persistence and storage failure;
individual document snapshots, binary reads, expiry, quotas, path/peer denial
and recursive revocation; declared interaction handlers, unsafe URI rejection,
trusted choice/defaults, acknowledgement, installation changes and lost launch
reply recovery; network policy validation/persistence/watches; and audio PCM
ownership, gain, focus, pause/toggle, live preference updates and restart.
The installed SDK checks compile and consume LeonPlatform through CMake and
pkg-config. The complete suite also retains the existing authentication, Root,
logging, sandbox, package, SDK and application-management checks.

Independent native Weston renders all eight apps, the shell and both shade
stages, and passes keyboard/input, lifecycle and Material motion checks. The
four local Buildroot components and the AArch64/musl ext4/initramfs/default disk
image build successfully. Four new supervised units start settingsd, documentd,
intentd and audiod; netd remains an independent supervised service.

The focused QEMU run passes the actual kernel boot and a real reboot, settings
broker compatibility, exact document transfer and revocation, real sandboxed
Editor delivery/acknowledgement and saved contents, PipeWire crash/restart and
stream invalidation, and synchronous capability reset during application data
clear. Settings, DNS overrides, default handlers and per-app audio policy survive
the reboot. Master volume reaches the actual output's requested 70%, including
the PipeWire device Route and WirePlumber cubic scale.

Actual compositor input exercises the app chooser and Always default, document
picker cancellation, background playback, lock-screen pause/resume, Settings
Ethernet disable/reconnect, per-app volume changes, default clearing, opening a
private Files document in Editor and Settings revocation of its source and
delegated grants. Clean service shutdown unmounts data and powers QEMU off.
The 124.2-second captured WAV contains simultaneous 440 Hz and 660 Hz tones in
the same analysis window, verifying actual output mixing rather than queue
consumption alone. The capture includes silence and the later Gallery test tone;
it is an integration fixture, not a continuous media-quality benchmark.

The separate combined QEMU regression also passes native authentication and
Root probes, logging/offline recovery/real ENOSPC, memory/power/storage/time,
DHCP renewal and client crash recovery, actual static IPv4/DNS and return to
DHCP, Landlock/seccomp/cgroups/core capture, terminal VT/PTY/vi/top/clipboard,
live notifications, Chinese IME restart and permission/file selection. All eight
built-ins, SDK package installation/removal, shade/navigation/lifecycle controls,
Settings application management, offline saved-data inspection and clean UI
poweroff pass against the final default image. Its evidence is in
`build/platform-qemu-regression.log` and `build/qemu-test/`.

Reproduce the focused check with `scripts/build-target-probes.sh` and
`python3 scripts/test-qemu.py --timeout 240 --platform-probe build/platform-probe`.
Evidence remains ignored in `build/platform-{native-build,ctest,python-final,wayland,
core-final,desktop-final,image-final,qemu}.log` and `build/qemu-platform-test/`,
including `platform-services.txt`, `platform-reboot.txt`, the serial trace,
captured WAV, desktop frames and `platform-overview.png`.

These results apply to the LeonMobile 1.0.0 QEMU developer graphics prototype.
Physical output/headphone switching, microphone capture, wireless/mobile radio
management and physical phones are unverified or outside these protocols.
URI dispatch requires an installed declared handler; this change does not add a
browser. Settings watches use resynchronizable generation polling. Documents
grant only temporary read access, and revocation cannot recall bytes already
read. Current images remain under `output/images/`; the older `dist/` bundle is
not regenerated.
## Per-application namespaces (2026-11-05)

The production launcher now requires mount/PID/IPC namespaces, a private
read-only root/proc view, bounded scratch mounts, minimal devices and a separate
devpts instance. The initial implementation passed 12 native CTest checks,
106 Python checks and the independent native Weston rendering/lifecycle/motion
smoke. The AArch64 core package, target probes and reference image rebuilt.

The focused QEMU `--core-only --system-probe build/system-probe` run passed all
system service checks and two real installed isolation fixtures. These cover
distinct namespace inodes, hidden cross-app homes/packages and host temp files,
SysV IPC separation, private PTY zero while a host PTY is occupied, device-node
absence, denied mount/namespace/mknod operations, capability/FD hygiene and
orphan reaping. Normal exit 37, detached-child exit 42, SIGTERM 143, SIGSEGV 139
with actual host-PID core capture, forced stop 137 and unexpected PID 1 death
all produced expected cleanup. Invalid staging/home symlinks failed closed;
broker SIGKILL/restart removed the old task and allowed a fresh launch.
Runtime timezone replacement reached an already running app through the public
directory bind. The real Chinese IME restart, permission deny/allow/file picker,
exact offline saved/imported data and clean shutdown checks passed afterward.

The raw-status retirement path was corrected after target fault injection
exposed EOF arriving before waitpid on forced stop. OOM priority writes now use
a pinned proc-directory descriptor instead of a recyclable numeric PID path.
Evidence remains ignored under `build/isolation-{ctest,python,wayland}.log`,
`build/isolation-qemu-core.log` and `build/qemu-test/`.

The separate third-party Root suite passed installed main/helper identity,
fresh approval, masked IME password entry, bad-password delay, cancellation,
block/unblock/history, exact helper output, revocation and actual reboot
persistence with the requesting app still sandboxed. The developer Root suite
also passed all three boot phases, runtime kernel/module/service controls and
two real `su` commands from Terminal's private PTY. The privileged PTY relayed
correctly through SCM_RIGHTS without exposing the host devpts mount. Its UI
fixture now follows the actual service catalogue's pages and scroll position
rather than assuming notificationd is on the first screen. Reports are in
`build/isolation-qemu-root-apps.log` and `build/isolation-qemu-root.log`.

The final combined QEMU regression passed authentication/Root probes, all
system/isolation fault probes, all eight built-in identities, terminal VT/PTY,
Ctrl-C, vi, top, clipboard and shell restart, live notification identity and
scrolling, Chinese IME restart/portal decisions, SDK package install/launch/remove,
Settings application stop/clear/uninstall, shade/navigation/settings and clean UI
poweroff. Offline disk inspection confirmed the exact saved/imported editor
contents and terminal probe, interrupt, edit, paste and restart results.
`build/isolation-qemu-full.log` records this run. Graphical suites were completed
serially; an earlier concurrent TCG run entered the existing fail-closed lock
path. These checks validate the QEMU developer prototype, not physical phones or
an independently audited production security boundary.

The private devpts mount now also enforces `max=64`. The rebuilt AArch64 image
passed the complete system/isolation target probe with fixtures checking exactly
64 masters, kernel `open` returning ENOSPC and musl `posix_openpt` translating it
to EAGAIN, followed by closing the masters and reusing `/dev/pts/0`. The combined
run then stopped in the existing Root session-capacity probe; investigation of
that compatibility failure follows separately. Its serial evidence is retained
in ignored `build/isolation-evidence/pty-namespace-serial.log`.

The Root compatibility investigation reproduced a policy-reader race during
atomic replacement: a descriptor opened before rename could have link count
zero at fstat, causing a valid policy generation to be treated as corrupt and
live sessions to be revoked. Readers now accept that pinned root-owned snapshot
while retaining private directory, owner, mode, no-symlink, extra-hardlink and
schema checks. Service output records the reason for revocation.

The new concurrent replacement regression failed against the previous reader
and passed ten consecutive runs after correction, including real session
survival and subsequent authenticated disable/revocation. The complete final
native suite passes 12 CTest and 107 Python cases, including corruption,
permissions/hardlinks, six-session capacity, password rotation, lockout and
third-party Root identity/revocation. The AArch64 core and reference image
rebuilt successfully. Evidence remains ignored in
`build/isolation-root-policy-{before,after}.log`, `build/isolation-{ctest,python}.log`
and `build/isolation-{build-core,image}.log`.

The final combined QEMU regression on the image containing both the 64-PTY
limit and policy snapshot correction passed all Root/authentication and
system/isolation probes, including the six-session capacity/revocation check.
All eight built-ins, actual terminal PTY/VT/Ctrl-C/vi/top/clipboard/restart,
notifications, Chinese IME/portal decisions, SDK package installation/removal,
Settings stop/clear/uninstall and clean UI poweroff passed. Offline inspection
confirmed exact editor/import contents and terminal probe/edit/restart data.
The final run is recorded in ignored `build/isolation-qemu-full.log` and
`build/qemu-test/`; it validates the current QEMU developer graphics prototype.

## Boot health and independent Recovery (2026-11-07)

`make test` passed 13 CTest checks and 110 Python tests. The native boot-state
fixtures cover persisted pending attempts, recovery after three unfinished
boots, safe/normal requests, boot-ID checks, checksum corruption and unsafe
filesystem objects. The independent native Wayland smoke passed all eight
built-ins, shell and both control-panel states, with lifecycle and motion
checks.

The AArch64 QEMU test used a disposable disk and passed real initial, safe and
normal boots; persistent device-PIN setup and lock/unlock; safe-mode app policy;
missing-essential-file entry into the independent Recovery archive; read-only
ext4 check; cancelled restore; invalid-image rejection without system writes;
Root-password-authenticated diagnostic export; matching-image restore and
read-back hash; and a subsequent normal boot with the PIN, Root policy,
third-party package, application-private data and home file retained. Direct
Recovery archive startup and return to the normal desktop also passed. The
restored third-party app launched and rendered. QEMU frames and serial logs are
ignored under `build/qemu-recovery-test/`; the screenshots are execution
evidence, not generated mockups.

The native state-machine tests cover the three-failure threshold, but the
180-second PID 1 deadline was not accelerated or waited out in QEMU. This run
does not test physical boot firmware, actual handset storage/power loss, secure
boot, signatures, or rollback resistance. Recovery is an early text console
and requires a matching host-supplied kernel/initramfs and verified local image.
