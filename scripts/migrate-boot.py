#!/usr/bin/env python3
"""Create a bootable disk from the current build, retaining the old disk's data."""
import argparse
import fcntl
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from leon_image import copy_region, hash_region, partitions
from qemu_boot import default_images

REPO = Path(__file__).resolve().parents[1]


def migrate(old, new, images):
    if new.exists() or old.resolve() == new.resolve():
        raise ValueError('Destination must be a new file; the original disk is never overwritten')
    baseline = images / 'leonmobile-qemu-aarch64.img'
    new.parent.mkdir(parents=True, exist_ok=True)
    with old.open('rb') as source, baseline.open('rb') as build:
        fcntl.lockf(source, fcntl.LOCK_SH | fcntl.LOCK_NB)
        fcntl.lockf(build, fcntl.LOCK_SH | fcntl.LOCK_NB)
        source_offset, size = partitions(source)['data']
        layout = partitions(build)
        if 'boot' not in layout:
            raise ValueError('The selected build is not a UEFI disk image')
        target_offset, capacity = layout['data']
        if size != capacity:
            raise ValueError('Data partition sizes differ; automatic resize is not supported')
        source.seek(source_offset + 1080)
        if source.read(2) != b'\x53\xef':
            raise ValueError('Original data partition is not ext4')
        fd, name = tempfile.mkstemp(prefix='.leon-migrate-', dir=new.parent)
        os.close(fd)
        try:
            subprocess.run(['cp', '--reflink=auto', '--sparse=always', baseline, name], check=True)
            with open(name, 'r+b') as target:
                source.seek(source_offset)
                target.seek(target_offset)
                expected = copy_region(source, target, size)
                target.flush()
                os.fsync(target.fileno())
                if hash_region(target, target_offset, size) != expected:
                    raise ValueError('Data partition read-back verification failed')
            os.link(name, new)  # Atomically publish, refusing any existing destination.
            directory = os.open(new.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        finally:
            os.unlink(name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('old', type=Path, help='stopped disk; remains unchanged')
    p.add_argument('new', type=Path, help='new disk with the current system/kernel and retained data')
    p.add_argument('--images', type=Path, default=default_images())
    a = p.parse_args()
    migrate(a.old, a.new, a.images)
    print('Created', a.new, 'with the current build and verified original data; original disk unchanged')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('Migration refused:', error, file=sys.stderr)
        sys.exit(2)
