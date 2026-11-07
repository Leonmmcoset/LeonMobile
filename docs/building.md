# Build, run and validate

The supported development host is Linux x86_64. This repository bootstraps a checksum-pinned Buildroot 2026.08.1 and an external AArch64 musl toolchain. A full initial build downloads several hundred MiB of source and needs several GiB of temporary storage; retain `.cache/downloads` between builds. Network access is needed for the first build.

## Host setup

On Debian/Ubuntu, install the cross-build prerequisites:

```sh
sudo apt-get install build-essential bison flex gawk gettext texinfo libssl-dev \
  libncurses-dev python3 python3-pil rsync cpio unzip bc file wget curl xz-utils \
  qemu-system-arm qemu-utils e2fsprogs fdisk mtools dosfstools git
```

Native component development additionally uses:

```sh
sudo apt-get install cmake ninja-build pkg-config libarchive-dev nlohmann-json3-dev \
  libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev libxkbcommon-dev \
  libvterm-dev libjpeg-dev libpipewire-0.3-dev weston fonts-wqy-zenhei fonts-dejavu-core \
  xvfb xdotool vim-tiny python3-pil
```

No system-wide installation of LeonMobile is required for native tests. `make test` builds native core/LMUI/apps and runs component, package and image update tests. Identity-transition tests require root; run them in the disposable CI environment or with `sudo env LEON_TEST_BIN="$PWD/build/host" python3 -m unittest discover -s tests -v`. The tests redirect stores, sockets and runtime directories into temporary paths. Native rendering can be checked using `scripts/test-wayland.sh`; it uses Weston as an independent protocol peer, not the target compositor, and captures PNGs under `build/screenshots`. `python3 scripts/test-wayland-input.py` verifies real keyboard/repeat/clipboard events; adding `--terminal` exercises the interactive shell, Ctrl-C, vi save, selection/paste and restart through Weston on an isolated Xvfb display.

`python3 scripts/test-wayland-input.py --developer` drives Settings against a
private rootd/proc fixture: initial password setup, parameter editing, disabling,
re-enabling and password rotation. It needs root for real peer credentials. No
host kernel parameters or modules are changed. After building the image and
`scripts/build-target-probes.sh`, run `python3 scripts/test-qemu.py --timeout 240
--root-only --root-probe build/root-probe` for actual module/sysctl/session tests,
developer Settings and sandboxed terminal input, and two real guest reboots.

## Build the reference image

```sh
make image
make run
```

`JOBS=4 make image` controls build parallelism. Buildroot configuration is generated from `distro/configs/leon_qemu_aarch64_defconfig` on the first build. After editing that defconfig, regenerate with:

```sh
./scripts/build.sh leon_qemu_aarch64_defconfig
```

Buildroot does not automatically rebuild already-built packages after changes to their configuration or local source. For local component changes use `./scripts/build.sh leonmobile-rebuild` followed by `make image`. A clean output tree is required when changing toolchains or incompatible foundational configuration. The download cache can be retained.

The `leonmobile` selection aggregates four source packages: `leon-core`, `lmui`,
`leon-desktop`, and `leon-apps`. Each package configures only its own CMake
targets. Desktop and applications consume LMUI from the installed staging SDK.
Use `./scripts/build.sh leon-apps-rebuild` (or the corresponding component name)
for a focused rebuild. Use `leonmobile-reconfigure` when changing the shared
CMake configuration; `leonmobile-rebuild` remains a convenience target for all
four packages. After migrating an existing output tree to these packages,
regenerate the reference defconfig once before building.

The four local packages use separate `buildroot-build` CMake directories. After
updating an older output tree that built these packages in their source
directories, remove their generated build trees once (`leon-core-dirclean`,
`lmui-dirclean`, `leon-desktop-dirclean`, `leon-apps-dirclean`) and run `make image`. This
prevents source synchronization from replacing a generated build Makefile with
the repository's native-development Makefile.

Target installation uses the `Runtime` component. LMUI headers, its link-time
library symlink, pkg-config file and CMake package belong to `Development` and
are installed into Buildroot's staging sysroot. See [development.md](development.md)
for component ownership, native build options and SDK consumption.

The outputs include `Image`, both initramfs archives, `rootfs.ext4`, `data.ext4`, `leonmobile-qemu-aarch64.img`, `leon-efi.vfat`, `leonmobile-recovery-aarch64.img` and the two `leon-uefi-*.fd` flash banks. `make run` boots through EDK II and the disk's GRUB; the host does not load the kernel/initramfs directly. It creates `build/leonmobile-user.img` and a separate `.vars.fd` file on first use, preserving them on subsequent runs. `LEON_DISK` selects another writable image. Old two-partition disks require explicit migration or `LEON_BOOT=direct`; see [disk-boot.md](disk-boot.md). The generated reference disk and firmware code bank remain read-only inputs.

The desktop is 480×960 portrait. Click the lock screen or swipe up to unlock. The status bar opens notifications on the left and quick settings on the right. Bottom gestures navigate home and tasks; three-button navigation can be selected in settings. Mouse clicks and drags act as single-touch input. The developer serial console automatically logs in as the non-root `leon` user.

`LEON_DISPLAY=none make run` runs without a visible window. `LEON_AUDIO=pa make run` selects a PulseAudio host backend when supported by QEMU and the host; the default `none` backend retains the guest audio device for automated tests without requiring a host sound server. Guest PipeWire/ALSA operation and host audible playback are separate checks. `LEON_IMAGES` selects the directory containing the reference disk and firmware artifacts. Press Esc at the LeonMobile startup prompt for the hidden menu; `LEON_BOOT_MODE=safe` requests a one-shot safe boot and `LEON_RECOVERY_MEDIA=1` attaches the independent USB recovery disk.

Boot logs appear on the portrait screen once the kernel initializes virtio-gpu,
followed by init/service status and the Wayland lock screen. The host terminal
continues to receive serial logs. All launchers, tests and recorders read
`distro/board/qemu-arm64/cmdline`; assembled releases include that file. The
VT-bound seat prevents later kernel/status messages from drawing over Wayland
and restores the text console when the compositor exits. See
[architecture.md](architecture.md#boot-and-storage) for output routing.

For focused boot ownership/recovery checks, compile the target probes and run:

```sh
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 180 --boot-console-probe build/boot-console-probe
```

This uses a private image and a test-only readiness barrier to inspect the real
text VT before Wayland starts. It checks kernel/init/service text, dual consoles,
graphics ownership, late-log drawing suppression, text recovery after compositor
stop/SIGKILL, and a real reboot. The barrier and privileged fixture are absent
from production images. Evidence is in `build/qemu-boot-console-test/`.

## Install and update

Build the example using the target SDK:

```sh
./scripts/build-example.sh
```

Transfer the resulting `build/hello/org.example.hello-1.0.0.lmpkg` into a readable guest data location using a development transport or offline disk tooling, then run `lmpkg install PATH` from the `leon` serial session. The prototype deliberately does not enable a network login or application repository.

Stop QEMU before updating a runtime disk:

```sh
python3 scripts/update-image.py build/leonmobile-user.img output/images/rootfs.ext4
```

The tool validates GPT checksums/layout and ext4 magic and acquires a POSIX lock before writing the system partition. Back up important development data separately. This replacement process preserves the data partition but is not an atomic OTA update. Kernel and initramfs are loaded from the current build outputs on the next run.

`make legal-info` gathers dependency license materials. `make release` assembles images, build provenance, a software manifest, documentation, legal materials and SHA-256 checksums under `dist/leonmobile-1.0.0-qemu-aarch64/`. The first assembly additionally downloads about 100 MiB of pinned musl/GCC runtime source and license material, cached under `.cache/toolchain-legal`. It supplements Buildroot's legal output with those files and the Buildroot source archive; the original Buildroot warnings remain in its README. The external host toolchain is documented through its README, manifest and build configuration. Release assembly does not sign images, create a Git tag, publish externally or certify production readiness.

Each assembly uses a fresh staging directory. When replacing an existing local assembly, it preserves that directory as `dist/.previous-*`. `build-info.json` records the Git commit and whether the source tree contained changes. Commit completed work before assembling the final candidate.

The assembled directory can be copied to another Linux host with QEMU installed:

```sh
cd dist/leonmobile-1.0.0-qemu-aarch64
sha256sum --check SHA256SUMS
./run.sh
```

The launcher creates a separate persistent `leonmobile-user.img` next to the release files. Use the same `LEON_DISK`, `LEON_DISPLAY` and `LEON_AUDIO` settings as the repository launcher. The included `scripts/update-image.py` can replace a stopped runtime disk's system partition.

## Recovery and boot-health validation

The reference image contains `leon-initramfs.cpio.gz` for normal startup and a
separate `leon-recovery.cpio.gz` for direct Recovery selection. Both have an
independent musl runtime and recovery tools. Start Recovery against a separate
development disk and attach the exact matching root image read-only:

```sh
LEON_DISK="$PWD/build/leonmobile-recovery-user.img" make recovery
LEON_DISK="$PWD/build/leonmobile-recovery-user.img" \
  LEON_REPAIR_IMAGE="$PWD/output/images/rootfs.ext4" make recovery
```

The text menu accepts keyboard or serial input. It offers normal/safe boot,
read-only ext4 checks, boot diagnostics and a confirmed system-only restore.
Restoring requires typing `RESTORE`; the image size, ext4 marker and SHA-256
must match the build metadata. `/data` holds credentials, packages and user
files and is not rewritten. The image hash pins a local artifact, not a
publisher signature. Recovery does not provide a shell or network service.

The end-to-end test uses its own temporary QEMU disk. It checks the recovery
archive directly, real safe and normal boots, policy overrides, a deliberate
missing system executable, read-only fsck, cancellation and invalid media
rejection, valid system replacement and offline verification of Root/PIN,
installed package, application-private data and user files:

```sh
./scripts/build-target-probes.sh
./scripts/build-example.sh
python3 scripts/test-recovery-qemu.py
```

QEMU logs and PNG screenshots are stored in `build/qemu-recovery-test/`. This
is reference-target evidence only; it does not validate hardware boot firmware,
secure boot, publisher signatures or physical-device recovery.

## Target validation

Date/time and notification Settings have a focused target check, including
manual local time, a real local SNTP exchange, captured notification sounds,
lock-screen privacy, daemon recovery and a real reboot:

```sh
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 240 --preferences-probe build/preferences-probe
```

Evidence is written to `build/qemu-preferences-test/`. The fixed-operation
privileged fixture is injected only into a disposable image. See
[date-time-notifications.md](date-time-notifications.md) for behavior and scope.

The platform-service integration check includes a real reboot, sandboxed app
delivery, revocable documents, two mixed PCM streams captured from QEMU's audio
device, backend recovery and the desktop chooser/picker/media/Settings flow:

```sh
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 240 --platform-probe build/platform-probe
```

Its serial output, service/reboot reports, display frames and captured WAV are
written to `build/qemu-platform-test/`. Tone analysis checks that the output
contains both 440 Hz and 660 Hz in the same capture window; consuming PCM alone
is insufficient. The privileged test helper is injected into a private image
and is absent from the shipping root filesystem.

`python3 scripts/test-qemu.py` boots an isolated disk copy with software-emulated ARM64. It verifies the non-root console, kernel version, nine distinct ELF manifests, launch/process identity for every built-in, portrait display, home-screen app launch, calculator input, persisted dark theme, actual PipeWire volume, DHCP/routing/DNS and clean compositor shutdown through the UI. It writes serial output, PPM display frames and network/audio status to `build/qemu-test`. On failure it saves `failed-disk.img`; journal segments are in that disk's data partition under `/leon/logs/` (mounted as `/data/leon/logs` in the guest). Use the built `output/host/sbin/debugfs` for newer ext4 features rather than older host tools.

`python3 scripts/test-qemu.py --logging-only --timeout 180` independently exercises
the production Settings diagnostics page, system-log access, detail/service/crash
tabs, focus/lock protection and prior history after a real reboot. It writes
`build/qemu-logging-test/`. Native logging/diagnostics checks and journal recovery
commands are documented in [logging.md](logging.md).

Include the cross-compiled example to exercise package transfer, checksum validation, install, launch and removal:

```sh
./scripts/build-example.sh
python3 scripts/test-qemu.py --package build/hello/org.example.hello-1.0.0.lmpkg
```

Exercise the graphical `.lmpkg` installer separately:

```sh
./scripts/build-target-probes.sh
python3 scripts/test-installer-qemu.py
```

This uses a disposable EFI disk, opens packages through Files and the system
picker, and checks explicit cancellation/confirmation, version/permission
review, damaged packages, installed application launch, retained identity/data
and a real reboot. Screenshots, reviews and serial evidence are written to
`build/qemu-installer-test/`. The seeding/observation probe is injected only into
the test disk; it is absent from the normal image. See
[package-installer.md](package-installer.md) for the UI and broker protocol.

Compile and include the target fault probes for the complete system-service,
isolation, notification and input-method check:

```sh
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 240 \
  --system-probe build/system-probe \
  --notification-probe build/notifications-probe \
  --terminal-probe build/terminal-probe \
  --package build/hello/org.example.hello-1.0.0.lmpkg
```

Probes are injected into a private cloned ext4 image and run with controlled
guest identities; they are not shipped in the default image. These checks
create real guest memory pressure and cgroup OOM, kill DHCP and input-method
processes, capture a fatal core, mount a temporary USB ext4 device, exercise
RTC/local UDP SNTP and test authorization. UI checks choose Chinese candidates,
delete a UTF-8 character, save an exact note, deny/approve a permission and pick
a file; after clean shutdown the private data partition is checked offline.
`--terminal-probe` additionally tests the sandboxed terminal with a VT/PTY
probe, ANSI RGB rendering, real Ctrl-C, vi editing, top, selection/clipboard
and shell restart. It checks exact saved files from ext4 after clean shutdown;
see [terminal.md](terminal.md) for controls and compatibility limits.
`--core-only --system-probe build/system-probe` runs the focused service/IME/
portal sequence. CI's optional image job runs the full command above. All
mutations occur inside QEMU; existing user disks are not used.

`--images dist/leonmobile-1.0.0-qemu-aarch64` validates an assembled release. CI runs native checks on every change; the full image/QEMU job is available through workflow dispatch. See [validation evidence](validation.md) for the tested scope and limits.

## Record a system tour

An actual QEMU boot and touch-driven tour can be recorded on a headless host:

```sh
sudo apt-get install ffmpeg xvfb xdotool python3-pil qemu-system-gui
python3 scripts/record-qemu.py --output build/recordings/LeonMobile-demo.mp4
```

The recorder uses a private disk copy, an isolated Xvfb display and QEMU's SDL
backend. FFmpeg continuously records the native 480×960 viewport at 30 fps as
H.264/yuv420p MP4, with playback chapters and fast-start metadata. The complete
boot, waits, application launches, animations and shutdown stay at their actual
speed. Kernel boot text remains on the serial console; the graphical capture
starts blank until the compositor appears. The tour has no audio track.

The tour opens Calculator, Settings, Clock, Editor, Files, the LMUI demo,
Images and Terminal from their icons. It calculates `7 + 3`,
changes settings, runs the stopwatch, types with the screen keyboard and saves
a note, creates a file, posts an LMUI notification, opens the shade, displays a
small PNG example, runs kernel/architecture commands in the terminal and
switches recent tasks before powering off through the UI. The PNG example is
temporary recording data, uploaded through the development console. Screenshots,
serial/encoder logs and a chapter timeline are stored beside the video in a
directory of the same name. The lock-screen screenshot is compared with the
recorded viewport to detect host decorations, scaling or cropping.

`--images DIR` selects another reference-image directory. An existing output is
rejected; use a new filename for another take. Videos and supporting evidence
belong under ignored `build/`, rather than in Git. This is a recording of the
QEMU developer prototype, with the same device and security limits as normal
target validation.

For the optional third-party Root SDK example, first build the system, then run
`./scripts/build-root-example.sh`. The generated package is under
`build/root-demo/`; it is not included in the default image. Root remains
disabled by default. The SDK's `LeonRoot::Root` API and separate helper manifest
are documented in `docs/root-access.md`.

Additional validation:

```sh
python3 scripts/test-root-wayland.py
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 240 \
  --root-apps-probe build/root-applications-probe \
  --root-apps-package build/root-demo/org.example.rootdemo-1.0.0.lmpkg
python3 scripts/record-root-apps.py --output build/recordings/LeonMobile-third-party-root.mp4
```

The focused QEMU check uses `build/qemu-root-apps-test/`, injects probes only into
a disposable image and performs a real guest reboot. The recorder uses the
production image and only copies the installable demo package into its private
disk; system confirmation and Root password entry use QMP keyboard/pointer input. Its
MP4 is an uncut 480×960, 30 fps silent capture of the QEMU viewport. Evidence,
images and temporary fixture packages stay outside version control.
