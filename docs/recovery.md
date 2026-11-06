# Boot health and independent Recovery

LeonMobile 1.0.0 implements this flow for the QEMU AArch64 reference target.
The host still loads the kernel and initramfs directly. This is not an A/B
updater, signed boot chain or physical phone recovery implementation.

## Entry and layout

`make run` uses `leon-initramfs.cpio.gz`, containing early init and a complete
Recovery runtime. `make recovery` loads the separate `leon-recovery.cpio.gz`
instead. Both include their own musl loader, required ELF libraries, BusyBox,
Recovery and e2fsck; they never execute tools from the system partition. Kernel
`panic=5` reboots an early kernel/init crash instead of hanging indefinitely.
A kernel that cannot run at all still requires host selection/replacement.

```sh
# Stop the guest first. Reuse the same LEON_DISK when specified for make run.
make recovery
# Attach the build-matched baseline as a read-only repair device.
LEON_REPAIR_IMAGE="$PWD/output/images/rootfs.ext4" make recovery
```

The recovery menu accepts the QEMU keyboard or serial console. It provides
normal/safe boot, an offline read-only filesystem check, confirmed system-image
restore, boot diagnostics, authenticated log export and poweroff. It starts no
shell or network listener. Repair requires typing `RESTORE`; cancel performs no
system write. Recovery leaves device and Root credentials intact.

From an authenticated Root session in the running guest:

```sh
leon-bootctl status
leon-bootctl next safe       # applies to the next reboot
leon-bootctl reboot recovery
leon-bootctl reboot normal
```

`next` and `reboot` accept only `normal`, `safe` or `recovery`. Root is required;
applications cannot forge boot success. The public `/run/leon-boot/status` reports
mode, health, attempts, consecutive failures, reason and kernel boot ID. The
CLI's `--state-dir` is for isolated native fixtures and disables reboot.

## Boot state and success

Early init mounts data first and durably records a pending attempt before
mounting the system read-only. The root-owned 0700 `/data/leon/boot` contains a
0600 checksummed state, serialized under a no-symlink lock and published using
atomic rename plus file/directory fsync. Invalid state enters Recovery rather
than silently resetting failure tracking. State is not a security boundary
against an authorized unrestricted Root user or offline disk editing.

Three unfinished normal/safe boots enter Recovery. A mount failure or missing
essential executable enters Recovery immediately. PID 1 checks the actual
service manager state and a root-only compositor response: core services must
be ready, authentication live, the shell mapped and a recent successful output
commit recorded after the lock screen becomes ready. Three stable seconds mark
the current kernel boot ID successful and reset the failure counter. Merely
starting a process or opening a Wayland socket is insufficient.

The startup deadline is 180 seconds. PID 1 owns it independently of leond's
restarts; expiry records the reason and reboots. Intentional shutdown before
success also counts as unfinished. Successful boot is an initial readiness
check, not an indefinite availability guarantee for every daemon.

## Safe mode

Safe mode lasts one boot. leond ignores the entire developer service-policy
directory, including later `reload`, while the application broker hides and
refuses to launch installed third-party apps. Built-in apps remain available,
and the home screen displays a safe-mode label. App data, installed packages,
Root policy, permissions and device credentials are retained; the lock screen
still authenticates normally. Safe mode also blocks manually launching a
third-party app through the broker, not only future automatic startup.

Return to normal by rebooting normally. After correcting a developer override,
normal boot can use it again. Recovery's explicit retry resets the failed-boot
counter; a subsequent failed repair or boot is still tracked.

## System restoration and interruption

The generated archives embed the SHA-256 and exact byte size of the matching
`rootfs.ext4`. QEMU attaches `LEON_REPAIR_IMAGE` as read-only `/dev/vdb`. Recovery
requires a read-only block source, matching size, ext4 magic and whole-image
hash. It writes only the fixed `/dev/vda1` partition, requires that partition to
be unmounted using exclusive block-device open, and checks exact capacity.
It never formats or rewrites `/dev/vda2`.

Before the first system write it fsyncs an interrupted-repair marker. It then
copies, fsyncs and hashes the target again. Only successful read-back clears the
marker. An interrupted restore returns to Recovery on the next boot and can be
retried with the same trusted medium. Restoring the baseline replaces all Root
changes on the system partition; user applications and data remain on data.
The hash pins a build artifact; it is not a publisher signature or rollback
protection. The host must supply a matching kernel/initramfs and trusted media.

## Diagnostics and privacy

The unauthenticated screen shows only boot reason/counters and the system
filesystem check. Export requires previously enabled Root and its current
password, including rootd's persistent retry limits. Recovery uses the same
verifier and never clears or changes credentials. The root-only
`/data/leon/boot/recovery-report.txt` contains boot details, current kernel text,
up to four bounded persistent log journals and bounded crash metadata. Core
payloads, application documents and credential stores are excluded. The report
is published atomically; a failed export preserves an older completed report.
Use an authenticated Root session or offline host tooling to copy it out.

If data cannot mount or its private state cannot be used, Recovery reports that
condition, permits read-only system diagnostics and poweroff, and refuses
restoration that cannot persist its interruption marker. It does not silently
format data or expose a passwordless maintenance shell.

## Validation

Native checks cover the state machine, failures/success/boot IDs, persistence,
corruption, unsafe links/modes, image hash/size/ext4 rejection and target-only
library resolution. Target validation uses disposable disks, actual failed
boots, Recovery input, safe-mode policy, restore/cancellation/rejection,
read-back and user-data preservation. Screenshots come from real QEMU output;
build evidence remains under ignored `build/`. See [validation.md](validation.md)
for completed runs and their limits.
