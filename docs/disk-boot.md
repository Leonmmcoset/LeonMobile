# QEMU AArch64 disk boot

The reference chain is QEMU flash -> EDK II ArmVirt -> the disk's
`EFI/BOOT/BOOTAA64.EFI` (GRUB) -> disk-resident Linux Image and initramfs ->
LeonMobile early init -> read-only system -> leond and Wayland.
`make run` does not pass `-kernel`, `-initrd` or `-append` to QEMU.
This developer reference chain has no Secure Boot, signatures, anti-rollback,
A/B slots or physical-phone support. SHA-256 lists detect accidental damage;
anyone who can rewrite the disk can also rewrite those lists.

## Artifacts and partitions

Buildroot builds EDK II `edk2-stable202602` and GRUB 2.14 from pinned,
hash-verified sources; `legal-info` collects their licenses and sources.
The kernel explicitly enables its AArch64 EFI stub, EFI variables filesystem and
EFI RTC driver. Under firmware-provided ACPI, the RTC is reached through EFI
runtime services rather than the direct-boot device-tree PL031 driver.
The board patch selects the unsupported-returning ArmTrngLib implementation:
QEMU 6.2 cannot service the newer SMCCC TRNG probe. The launcher supplies a
`virtio-rng-pci` device backed by the host `/dev/urandom`; EDK II's VirtioRngDxe
and the kernel use that entropy source. No timer-based replacement is enabled.

| Artifact | Purpose |
| --- | --- |
| `leonmobile-qemu-aarch64.img` | GPT: system #1 (768 MiB ext4), data #2 (512 MiB ext4), EFI boot #3 (128 MiB FAT32) |
| `leon-efi.vfat` | Build-matched EFI partition for offline repair |
| `leonmobile-recovery-aarch64.img` | Independent GPT/FAT32 medium with its own GRUB, kernel and Recovery initramfs |
| `leon-uefi-code.fd` | 64 MiB read-only EDK II code bank |
| `leon-uefi-vars.fd` | 64 MiB template for a writable variable bank per runtime disk |

The launcher clones the reference disk on first use. Default runtime files are
`build/leonmobile-user.img` and `build/leonmobile-user.img.vars.fd`.
`LEON_DISK`, `LEON_IMAGES` and `LEON_UEFI_VARS` override these paths.
System/data numbering stays compatible with early init and Recovery's fixed
read-only `/dev/vdb` system repair source. The standalone medium uses USB storage
so `/dev/vda` and `/dev/vdb` retain their existing purposes.

The EFI partition contains `EFI/BOOT/{BOOTAA64.EFI,grub.cfg,recovery.cfg,grubenv}` and
`EFI/LeonMobile/{normal,recovery}/{Image,initramfs,SHA256SUMS}`. Recovery uses a
separate kernel copy. The EFI partition is not mounted into the desktop or apps.

## Selecting startup

Default startup waits three seconds with the menu hidden. Press **Esc** after
the LeonMobile startup prompt to show ordinary startup, safe mode, Recovery and
power off. Use keyboard arrows and Enter; this firmware menu has no touch UI.
EDK II has a separate, earlier firmware menu.

```sh
make run
LEON_BOOT_MODE=safe make run
make recovery
LEON_REPAIR_IMAGE="$PWD/output/images/rootfs.ext4" make recovery
LEON_RECOVERY_MEDIA=1 make run
```

Host-selected normal/safe/recovery mode is written to a bounded GRUB environment
block while the guest is stopped; GRUB clears it before booting. Mode selection
requires host `mtools`. In userspace, `leon-bootctl` and the persistent boot-health
policy continue to control subsequent startup, including Recovery's normal/safe
retry. An attached standalone medium remains the preferred firmware boot device;
detach it after powering off when the main disk is ready to boot independently.

GRUB verifies ordinary kernel/initramfs hashes before loading them. A missing or
damaged payload or load error tries the independent recovery payload. The EFI
executable embeds an emergency path to the separate `recovery.cfg` for a missing
`grub.cfg`. Its embedded bootstrap deliberately uses only GRUB rescue-parser
commands; hash checks and menus run through the normal parser. If the executable,
FAT filesystem or EFI partition is unreadable, attach the standalone medium.
Replace damaged boot files from a trusted local build with the offline tool
below. A kernel that loads but stalls before early init is outside the existing
userspace health monitor; this does not implement a hardware watchdog.

Menu entries are usable without a GRUB password. Editing and the GRUB command
line require an account, and no account is registered. This prevents accidental
entry into a privileged GRUB shell; it does not secure unsigned, unencrypted
storage against someone who can replace boot media or firmware.

## Existing disks and offline repairs

Stop QEMU first. An old two-partition disk is never silently repartitioned.
Create a new disk with the **current build's system and boot files**, retaining
the complete old data partition, and explicitly select the new disk:

```sh
python3 scripts/migrate-boot.py build/leonmobile-user.img build/leonmobile-uefi.img
LEON_DISK="$PWD/build/leonmobile-uefi.img" make run
```

Migration refuses an existing destination, invalid GPT, incompatible data sizes
or non-ext4 data. It locks the input, verifies the copied data by reading it back,
flushes the new disk and publishes it without replacing the original. No resizing,
schema downgrade or cross-release migration is implemented.

Boot repair requires the trusted local build matching the installed system. It
validates the layout, FAT32 image size and ARM64 EFI executable, restores only
partition #3, flushes it and verifies the written bytes:

```sh
python3 scripts/repair-boot.py build/leonmobile-uefi.img
```

This offline write is not atomic; repeat it if interrupted. System and data are
not written. `update-image.py DISK rootfs.ext4` still replaces only the system
and preserves EFI. After a kernel/initramfs change it must be paired with
matching boot repair before startup. Prefer a new migrated disk for full-build
changes. On-device Recovery restores only the system using its own pinned hash.

For diagnosis or an unmodified legacy disk, `LEON_BOOT=direct make run` explicitly
loads the host Image/initramfs. It bypasses UEFI/GRUB and is never an automatic
fallback. The standard QEMU test and recorders use disk boot;
`test-qemu.py --boot direct` retains the diagnostic path.

## Validation

```sh
make test
python3 scripts/test-disk-boot-qemu.py
python3 scripts/test-qemu.py
python3 scripts/test-recovery-qemu.py
```

The disk test uses disposable disks and verifies the actual firmware path,
keyboard menu selection, safe/normal startup, an actual kernel reboot, EFI RTC,
one-shot Recovery, legacy migration, missing or damaged boot payloads, missing
menu configuration, a destroyed EFI filesystem, independent USB recovery,
boot-only repair and successful startup afterwards. Commands, serial output,
status evidence and PNG screenshots are saved under `build/qemu-disk-boot-test/`.
Existing user disks are not used by these tests.
