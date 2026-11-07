#!/usr/bin/env python3
"""Stage the disk-resident EFI boot chain and an independent recovery medium."""
import argparse
import hashlib
from pathlib import Path
import shutil

from leon_image import grub_environment

REPO = Path(__file__).resolve().parents[1]
FLASH_SIZE = 64 * 1024 * 1024


def render_config(board, recovery_only=False):
    cmdline = (board / 'cmdline').read_text().strip()
    # Do not inject GRUB script syntax through a kernel command line.
    if not cmdline or any(c in cmdline for c in '\n\r;{}$\'"`\\'):
        raise ValueError('Unsupported kernel command line syntax')
    entries = '''menuentry 'LeonMobile 1.0.0' --id normal --unrestricted {
    leon_system
}
menuentry 'LeonMobile 1.0.0 - Safe mode' --id safe --unrestricted {
    leon_system leon.safe=1
}'''
    default = '''if [ -f $prefix/grubenv ]; then
    load_env --file=$prefix/grubenv next_entry
    if [ "$next_entry" = normal -o "$next_entry" = safe -o "$next_entry" = recovery ]; then
        set default=$next_entry
        set timeout=0
    fi
    set next_entry=
    if ! save_env --file=$prefix/grubenv next_entry; then
        echo LEON_BOOT_REQUEST_CLEAR_FAILED
    fi
fi'''
    if recovery_only:
        default = 'set default=recovery\nset timeout=0'
        entries = ''
    return ((board / 'grub.cfg.in').read_text()
            .replace('@CMDLINE@', cmdline).replace('@DEFAULT@', default)
            .replace('@SYSTEM_ENTRIES@', entries))


def stage_boot(images, board):
    loader = images / 'efi-part/EFI/BOOT/bootaa64.efi'
    if not loader.is_file():
        raise FileNotFoundError('Build the ARM64 EFI GRUB package first: ' + str(loader))
    for stage_name, recovery_only in (('leon-efi', False), ('leon-recovery-efi', True)):
        stage = images / stage_name
        if stage.exists():
            shutil.rmtree(stage)
        boot = stage / 'EFI/BOOT'
        boot.mkdir(parents=True)
        shutil.copyfile(loader, boot / 'BOOTAA64.EFI')
        (boot / 'grub.cfg').write_text(render_config(board, recovery_only))
        (boot / 'recovery.cfg').write_text(render_config(board, True))
        (boot / 'grubenv').write_bytes(grub_environment())
        for name, archive in (('normal', 'leon-initramfs.cpio.gz'), ('recovery', 'leon-recovery.cpio.gz')):
            if recovery_only and name == 'normal':
                continue
            payload = stage / 'EFI/LeonMobile' / name
            payload.mkdir(parents=True)
            hashes = []
            for source, dest in (('Image', 'Image'), (archive, 'initramfs')):
                shutil.copyfile(images / source, payload / dest)
                with (payload / dest).open('rb') as stream:
                    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
                hashes.append(f'{digest}  /EFI/LeonMobile/{name}/{dest}\n')
            (payload / 'SHA256SUMS').write_text(''.join(hashes))
    # ArmVirt maps two 64 MiB flash banks. The code bank is always read-only;
    # each running disk receives its own copy of the empty variable bank.
    for source_name, dest in (('QEMU_EFI.fd', 'leon-uefi-code.fd'), ('QEMU_VARS.fd', 'leon-uefi-vars.fd')):
        source = images / source_name
        if source_name == 'QEMU_EFI.fd' and not source.is_file():
            raise FileNotFoundError('Build the EDK II firmware first')
        if source.is_file() and source.stat().st_size > FLASH_SIZE:
            raise ValueError('Firmware exceeds the QEMU flash bank')
        with (images / dest).open('wb') as output:
            if source.is_file():
                with source.open('rb') as stream:
                    shutil.copyfileobj(stream, output)
            output.truncate(FLASH_SIZE)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--images', type=Path, required=True)
    p.add_argument('--board', type=Path, default=REPO / 'distro/board/qemu-arm64')
    a = p.parse_args()
    stage_boot(a.images, a.board)


if __name__ == '__main__':
    main()
