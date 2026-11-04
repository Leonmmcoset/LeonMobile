# Build, run and validate

The supported development host is Linux x86_64. This repository bootstraps a checksum-pinned Buildroot 2026.08.1 and an external AArch64 musl toolchain. A full initial build downloads several hundred MiB of source and needs several GiB of temporary storage; retain `.cache/downloads` between builds. Network access is needed for the first build.

## Host setup

On Debian/Ubuntu, install the cross-build prerequisites:

```sh
sudo apt-get install build-essential bison flex gawk gettext texinfo libssl-dev \
  libncurses-dev python3 rsync cpio unzip bc file wget curl xz-utils \
  qemu-system-arm qemu-utils e2fsprogs fdisk
```

Native component development additionally uses:

```sh
sudo apt-get install cmake ninja-build pkg-config libarchive-dev nlohmann-json3-dev \
  libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev libxkbcommon-dev \
  libvterm-dev libjpeg-dev weston fonts-wqy-zenhei fonts-dejavu-core \
  xvfb xdotool vim-tiny python3-pil
```

No system-wide installation of LeonMobile is required for native tests. `make test` builds native core/LMUI/apps and runs component, package and image update tests. Identity-transition tests require root; run them in the disposable CI environment or with `sudo env LEON_TEST_BIN="$PWD/build/host" python3 -m unittest discover -s tests -v`. The tests redirect stores, sockets and runtime directories into temporary paths. Native rendering can be checked using `scripts/test-wayland.sh`; it uses Weston as an independent protocol peer, not the target compositor, and captures PNGs under `build/screenshots`. `python3 scripts/test-wayland-input.py` verifies real keyboard/repeat/clipboard events; adding `--terminal` exercises the interactive shell, Ctrl-C, vi save, selection/paste and restart through Weston on an isolated Xvfb display.

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

Target installation uses the `Runtime` component. LMUI headers, its link-time
library symlink, pkg-config file and CMake package belong to `Development` and
are installed into Buildroot's staging sysroot. See [development.md](development.md)
for component ownership, native build options and SDK consumption.

The relevant outputs in `output/images` are `Image`, `leon-initramfs.cpio.gz`, `rootfs.ext4`, `data.ext4`, and `leonmobile-qemu-aarch64.img`. `make run` creates a separate writable disk at `build/leonmobile-user.img` on first use. Repeated runs reuse that disk, preserving data. `LEON_DISK` selects another writable image. The generated reference image is never used as a writable runtime disk by the launcher.

The desktop is 480×960 portrait. Click the lock screen or swipe up to unlock. The status bar opens notifications on the left and quick settings on the right. Bottom gestures navigate home and tasks; three-button navigation can be selected in settings. Mouse clicks and drags act as single-touch input. The developer serial console automatically logs in as the non-root `leon` user.

`LEON_DISPLAY=none make run` runs without a visible window. `LEON_AUDIO=pa make run` selects a PulseAudio host backend when supported by QEMU and the host; the default `none` backend retains the guest audio device for automated tests without requiring a host sound server. Guest PipeWire/ALSA operation and host audible playback are separate checks. `LEON_IMAGES` selects a directory containing the kernel, initramfs and reference disk.

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

## Target validation

`python3 scripts/test-qemu.py` boots an isolated disk copy with software-emulated ARM64. It verifies the non-root console, kernel version, eight distinct ELF manifests, launch/process identity for every built-in, portrait display, home-screen app launch, calculator input, persisted dark theme, actual PipeWire volume, DHCP/routing/DNS and clean compositor shutdown through the UI. It writes serial output, PPM display frames and network/audio status to `build/qemu-test`. On failure it saves `failed-disk.img`; logs are in that disk's data partition under `/var/log/leon/`. Use the built `output/host/sbin/debugfs` for newer ext4 features rather than older host tools.

Include the cross-compiled example to exercise package transfer, checksum validation, install, launch and removal:

```sh
./scripts/build-example.sh
python3 scripts/test-qemu.py --package build/hello/org.example.hello-1.0.0.lmpkg
```

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

The tour opens all eight built-ins from their icons. It calculates `7 + 3`,
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
