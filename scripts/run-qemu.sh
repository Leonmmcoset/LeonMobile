#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
images=${LEON_IMAGES:-"$repo/output/images"}
disk=${LEON_DISK:-"$repo/build/leonmobile-user.img"}
mkdir -p "$(dirname -- "$disk")"
if [ ! -e "$disk" ]; then
    cp --reflink=auto "$images/leonmobile-qemu-aarch64.img" "$disk"
fi
display=${LEON_DISPLAY:-gtk}
cmdline=$(cat "$repo/distro/board/qemu-arm64/cmdline")
initramfs="$images/leon-initramfs.cpio.gz"
if [ "${LEON_RECOVERY:-0}" = 1 ]; then initramfs="$images/leon-recovery.cpio.gz"; fi
recovery_args=()
if [ -n "${LEON_REPAIR_IMAGE:-}" ]; then
    recovery_args=(-drive "if=none,file=$LEON_REPAIR_IMAGE,format=raw,readonly=on,id=repair" -device virtio-blk-pci,drive=repair)
fi
exec qemu-system-aarch64 -M virt -cpu cortex-a72 -smp 4 -m 2048 \
    -kernel "$images/Image" -initrd "$initramfs" \
    -append "$cmdline" \
    -drive "if=none,file=$disk,format=raw,id=system" -device virtio-blk-pci,drive=system \
    -device virtio-gpu-pci,xres=480,yres=960 -device qemu-xhci -device usb-kbd -device usb-tablet \
    -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
    -audiodev "${LEON_AUDIO:-none},id=audio" -device intel-hda -device hda-duplex,audiodev=audio \
    -display "$display" -serial mon:stdio "${recovery_args[@]}" "$@"
