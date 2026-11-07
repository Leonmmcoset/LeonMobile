#!/usr/bin/env python3
"""Launch the reference disk through UEFI; retain explicit direct boot for diagnosis."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from qemu_boot import REPO, boot_arguments, default_images, qemu_path


def main():
    images = default_images().resolve()
    disk = Path(os.environ.get('LEON_DISK', REPO / 'build/leonmobile-user.img')).resolve()
    disk.parent.mkdir(parents=True, exist_ok=True)
    if not disk.exists():
        fd, name = tempfile.mkstemp(prefix='.leon-new-disk-', dir=disk.parent)
        os.close(fd)
        try:
            subprocess.run(['cp', '--reflink=auto', '--sparse=always',
                            images / 'leonmobile-qemu-aarch64.img', name], check=True)
            os.link(name, disk)  # Never replace a disk created by another launcher.
        finally:
            os.unlink(name)
    mode = os.environ.get('LEON_BOOT_MODE')
    if os.environ.get('LEON_RECOVERY', '0') == '1':
        mode = 'recovery'
    args = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-smp', '4', '-m', '2048',
            '-drive', f'if=none,file={qemu_path(disk)},format=raw,id=system',
            '-device', 'virtio-blk-pci,drive=system,bootindex=2',
            '-device', 'virtio-gpu-pci,xres=480,yres=960', '-device', 'qemu-xhci',
            '-device', 'usb-kbd', '-device', 'usb-tablet',
            '-netdev', 'user,id=net0', '-device', 'virtio-net-pci,netdev=net0',
            '-audiodev', os.environ.get('LEON_AUDIO', 'none') + ',id=audio',
            '-device', 'intel-hda', '-device', 'hda-duplex,audiodev=audio',
            '-display', os.environ.get('LEON_DISPLAY', 'gtk'), '-serial', 'mon:stdio']
    if os.environ.get('LEON_REPAIR_IMAGE'):
        args += ['-drive', 'if=none,file=' + qemu_path(os.environ['LEON_REPAIR_IMAGE']) +
                 ',format=raw,readonly=on,id=repair', '-device', 'virtio-blk-pci,drive=repair']
    args += boot_arguments(images, disk, Path(os.environ.get('LEON_UEFI_VARS', str(disk) + '.vars.fd')),
                           method=os.environ.get('LEON_BOOT', 'uefi'), mode=mode,
                           recovery_media=os.environ.get('LEON_RECOVERY_MEDIA', '0') == '1')
    os.execvp(args[0], args + sys.argv[1:])


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('LeonMobile boot:', error, file=sys.stderr)
        sys.exit(2)
