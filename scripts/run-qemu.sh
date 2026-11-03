#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
images="$repo/output/images"
disk=${LEON_DISK:-"$repo/build/leonmobile-user.img"}
mkdir -p "$(dirname -- "$disk")"
if [ ! -e "$disk" ]; then
    cp --reflink=auto "$images/leonmobile-qemu-aarch64.img" "$disk"
fi
display=${LEON_DISPLAY:-gtk}
exec qemu-system-aarch64 -M virt -cpu cortex-a72 -smp 4 -m 2048 \
    -kernel "$images/Image" -append 'console=ttyAMA0 root=/dev/vda1 rootwait ro init=/sbin/leon-init' \
    -drive "if=none,file=$disk,format=raw,id=system" -device virtio-blk-pci,drive=system \
    -device virtio-gpu-pci,xres=480,yres=960 -device qemu-xhci -device usb-kbd -device usb-tablet \
    -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
    -audiodev "${LEON_AUDIO:-none},id=audio" -device intel-hda -device hda-duplex,audiodev=audio \
    -display "$display" -serial mon:stdio "$@"
