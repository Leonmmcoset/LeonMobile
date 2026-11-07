#!/usr/bin/env python3
"""Restore only a stopped disk's EFI partition from a trusted local build."""
import argparse
import fcntl
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from leon_image import copy_region, hash_region, partitions
from qemu_boot import default_images

REPO = Path(__file__).resolve().parents[1]


def repair(disk, image):
    if disk.resolve() == image.resolve():
        raise ValueError('Disk and EFI image must be distinct files')
    with disk.open('r+b') as target, image.open('rb') as source:
        fcntl.lockf(target, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fcntl.lockf(source, fcntl.LOCK_SH | fcntl.LOCK_NB)
        layout = partitions(target)
        if 'boot' not in layout:
            raise ValueError('Disk has no EFI partition; migrate it instead')
        offset, capacity = layout['boot']
        if os.fstat(source.fileno()).st_size != capacity:
            raise ValueError('EFI image must exactly match the partition capacity')
        header = source.read(512)
        if header[510:512] != b'\x55\xaa' or header[82:90] != b'FAT32   ':
            raise ValueError('Source is not a FAT32 EFI partition')
        with tempfile.TemporaryDirectory(prefix='leon-check-efi-') as directory:
            loader = Path(directory) / 'BOOTAA64.EFI'
            subprocess.run(['mcopy', '-i', image, '::/EFI/BOOT/BOOTAA64.EFI', loader],
                           check=True, stdout=subprocess.DEVNULL)
            data = loader.read_bytes()
            if len(data) < 64 or data[:2] != b'MZ':
                raise ValueError('Invalid EFI executable')
            pe = struct.unpack_from('<I', data, 60)[0]
            if pe + 6 > len(data) or data[pe:pe + 6] != b'PE\0\0\x64\xaa':
                raise ValueError('EFI executable is not AArch64')
        source.seek(0)
        target.seek(offset)
        expected = copy_region(source, target, capacity)
        target.flush()
        os.fsync(target.fileno())
        if hash_region(target, offset, capacity) != expected:
            raise ValueError('EFI partition read-back verification failed')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('disk', type=Path)
    p.add_argument('--images', type=Path, default=default_images(),
                   help='trusted build matching the installed system')
    a = p.parse_args()
    repair(a.disk, a.images / 'leon-efi.vfat')
    print('EFI partition restored and read-back verified; system and data partitions preserved')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('Boot repair failed:', error, file=sys.stderr)
        sys.exit(2)
