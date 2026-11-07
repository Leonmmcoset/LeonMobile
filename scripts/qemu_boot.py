"""UEFI and explicitly selected direct-kernel boot for QEMU tools."""
import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from leon_image import grub_environment, partitions

REPO = Path(__file__).resolve().parents[1]
FLASH_SIZE = 64 * 1024 * 1024


def default_images():
    if 'LEON_IMAGES' in os.environ:
        return Path(os.environ['LEON_IMAGES'])
    if (REPO / 'leonmobile-qemu-aarch64.img').is_file():
        return REPO  # Self-contained local release.
    return REPO / 'output/images'


def qemu_path(path):
    return str(path).replace(',', ',,')


def request_mode(disk, mode):
    """Write one validated GRUB environment block while the guest is stopped."""
    block = grub_environment(mode)
    with disk.open('r+b') as stream:
        fcntl.lockf(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        layout = partitions(stream)
        if 'boot' not in layout:
            raise ValueError('This disk has no EFI partition; migrate it before selecting UEFI boot')
        offset, _ = layout['boot']
        with tempfile.TemporaryDirectory(prefix='leon-boot-request-') as directory:
            source = Path(directory) / 'grubenv'
            source.write_bytes(block)
            subprocess.run(['mcopy', '-o', '-i', f'{disk}@@{offset}', source,
                            '::/EFI/BOOT/grubenv'], check=True)
        os.fsync(stream.fileno())


def boot_arguments(images, disk, variables, *, method='uefi', mode=None, recovery_media=False):
    if mode not in (None, 'normal', 'safe', 'recovery'):
        raise ValueError('LEON_BOOT_MODE must be normal, safe or recovery')
    if method == 'direct':
        if recovery_media:
            raise ValueError('Standalone recovery media requires UEFI boot')
        cmdline = (REPO / 'distro/board/qemu-arm64/cmdline').read_text().strip()
        if mode == 'safe':
            cmdline += ' leon.safe=1'
        archive = 'leon-recovery.cpio.gz' if mode == 'recovery' else 'leon-initramfs.cpio.gz'
        return ['-kernel', str(images / 'Image'), '-initrd', str(images / archive), '-append', cmdline]
    if method != 'uefi':
        raise ValueError('LEON_BOOT must be uefi or direct')
    if not recovery_media:
        with disk.open('rb') as stream:
            if 'boot' not in partitions(stream):
                raise ValueError('Legacy disk has no EFI partition. Use scripts/migrate-boot.py OLD NEW '
                                 'or select LEON_BOOT=direct explicitly; the old disk was not changed')
    code = images / 'leon-uefi-code.fd'
    template = images / 'leon-uefi-vars.fd'
    if code.stat().st_size != FLASH_SIZE or template.stat().st_size != FLASH_SIZE:
        raise ValueError('UEFI flash images must each be 64 MiB; rebuild the image')
    if variables.resolve() in (code.resolve(), template.resolve(), disk.resolve()):
        raise ValueError('Runtime UEFI variables must use a separate file')
    variables.parent.mkdir(parents=True, exist_ok=True)
    try:
        with variables.open('xb') as output, template.open('rb') as source:
            shutil.copyfileobj(source, output)
    except FileExistsError:
        pass
    if variables.stat().st_size != FLASH_SIZE:
        raise ValueError('Invalid UEFI variable file size; select a fresh LEON_UEFI_VARS path')
    if mode and not recovery_media:
        request_mode(disk, mode)
    args = ['-drive', f'if=pflash,format=raw,readonly=on,file={qemu_path(code)}',
            '-drive', f'if=pflash,format=raw,file={qemu_path(variables)}',
            '-boot', 'strict=on,menu=off',
            '-object', 'rng-random,filename=/dev/urandom,id=leon-rng',
            '-device', 'virtio-rng-pci,rng=leon-rng,max-bytes=1024,period=1000']
    if recovery_media:
        args += ['-drive', f'if=none,file={qemu_path(images / "leonmobile-recovery-aarch64.img")},'
                 'format=raw,readonly=on,id=rescue', '-device', 'usb-storage,drive=rescue,bootindex=1']
    return args
