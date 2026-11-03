# LeonMobile 1.0.0 validation

The developer prototype was built and tested on 2026-11-03 using an x86_64 Linux host and QEMU software emulation of AArch64. This record describes observed behavior; it does not certify a physical phone or a production release.

| Check | Evidence |
| --- | --- |
| Native core, LMUI and apps | Four CTest checks passed: service configuration/dependencies, widget layout/hit testing/rendering, application models and keyboard callback lifetime/input |
| Native integration | Eleven Python checks passed, covering real UID transitions, private app data, broker authorization, package transactions/rejections, dependency-preserving shutdown and GPT/data preservation |
| Independent Wayland peer | All eight independent application executables and the shell rendered under headless Weston; PNGs in `build/screenshots/` |
| Target build | Complete Buildroot image; Linux reports `7.3.1-leonmobile`; target executables are AArch64 ELF with the musl loader |
| Target graphics | Actual DRM/pixman compositor, 480×960 display, Chinese lock/home/app screens and pointer input |
| Target applications | Eight distinct ELF entry points verified; retired dispatcher absent; calculator and settings launched from the desktop; calculator-result frame shows `7 + 3 = 10` |
| Settings and audio | Dark theme persisted; slider request of 25% matched the real PipeWire default sink volume of `0.25` |
| Network | DHCP address `10.0.2.15`, default gateway `10.0.2.2`, resolver `10.0.2.3`; gateway ICMP succeeded |
| SDK package | Example cross-compiled with the installed SDK, transferred with checksum verification, installed, launched as a native ELF app and removed through the broker |
| Shutdown | Quick-settings poweroff stopped the compositor successfully, stopped dependencies in order, unmounted data and powered down QEMU |
| Local distribution | Images, launcher, provenance, 136-entry software manifest, source/license material and SHA-256 file inventory assembled under `dist/` |

Reproduce the main checks:

```sh
make test
./scripts/test-wayland.sh
make image
./scripts/build-example.sh
python3 scripts/test-qemu.py --package build/hello/org.example.hello-1.0.0.lmpkg
make release
python3 scripts/test-qemu.py --images dist/leonmobile-1.0.0-qemu-aarch64 \
  --package dist/leonmobile-1.0.0-qemu-aarch64/examples/org.example.hello-1.0.0.lmpkg
```

QEMU evidence is written to `build/qemu-test/serial.log`, `network.txt`, `audio.txt` and PPM display frames. The calculator result was also inspected visually. Build and download outputs are excluded from Git; completed implementation steps are committed separately.

The eight application executables were all rendered against native Weston; the target interaction path covers calculator, settings and the SDK example. Host audible playback, external DNS name resolution, microphone recording, physical touch hardware and other phone peripherals were not verified. The headless QEMU check uses the `none` host audio backend. It verifies the guest audio device, graph and volume, without asserting audible sound. Security, device support and update boundaries are documented in [architecture.md](architecture.md).
