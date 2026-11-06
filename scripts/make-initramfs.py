#!/usr/bin/env python3
"""Assemble independent early/recovery userspace with target ELF dependencies."""
import argparse
import gzip
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess


def target_file(root, name):
    # Absolute target symlinks must never resolve against the build host's /lib.
    parts = list(PurePosixPath('/' + str(name).lstrip('/')).parts[1:])
    done = []
    links = 0
    while parts:
        part = parts.pop(0)
        if part in ('', '.'):
            continue
        if part == '..':
            if not done:
                raise ValueError('target path escapes root')
            done.pop()
            continue
        candidate = root.joinpath(*done, part)
        if candidate.is_symlink():
            links += 1
            if links > 40:
                raise ValueError('symlink cycle')
            link = PurePosixPath(os.readlink(candidate))
            if link.is_absolute():
                done = []
            parts = list(link.parts[1:] if link.is_absolute() else link.parts) + parts
        else:
            done.append(part)
    result = root.joinpath(*done)
    if not result.is_file():
        raise FileNotFoundError(result)
    return result


def assemble(target, stage, images, readelf, epoch):
    if stage.exists():
        shutil.rmtree(stage)
    for name in ('bin', 'sbin', 'lib', 'dev', 'proc', 'sys', 'newroot', 'data', 'run', 'tmp', 'etc/leon-recovery'):
        (stage / name).mkdir(parents=True, exist_ok=True)
    copied = set()

    def add(source_name, destination=None):
        destination = destination or source_name.lstrip('/')
        if destination in copied:
            return
        copied.add(destination)
        source = target_file(target, source_name)
        out = stage / destination
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, out)
        out.chmod(source.stat().st_mode & 0o777)
        if source.read_bytes()[:4] != b'\x7fELF':
            return
        dynamic = subprocess.check_output([readelf, '-d', source], text=True)
        for needed in re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic):
            for folder in ('lib', 'usr/lib'):
                try:
                    target_file(target, folder + '/' + needed)
                except FileNotFoundError:
                    continue
                add(folder + '/' + needed, 'lib/' + needed)
                break
            else:
                raise RuntimeError('Missing target library: ' + needed)
        program = subprocess.check_output([readelf, '-l', source], text=True)
        for loader in re.findall(r'Requesting program interpreter: (.*?)\]', program):
            add(loader, loader.lstrip('/'))

    add('/usr/sbin/leon-init', 'init')
    add('/usr/sbin/leon-recovery', 'sbin/leon-recovery')
    add('/bin/busybox')
    add('/sbin/e2fsck', 'sbin/e2fsck')
    image = images / 'rootfs.ext4'
    with image.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    (stage / 'etc/leon-recovery/system.sha256').write_text(digest + '\n')
    (stage / 'etc/leon-recovery/system.size').write_text(str(image.stat().st_size) + '\n')
    for recovery, filename in ((False, 'leon-initramfs.cpio.gz'), (True, 'leon-recovery.cpio.gz')):
        if recovery:
            (stage / 'recovery-mode').write_text('LeonMobile recovery\n')
        paths = sorted([stage, *stage.rglob('*')], key=lambda p: str(p.relative_to(stage)))
        for path in paths:
            os.utime(path, (epoch, epoch), follow_symlinks=False)
        names = b'\0'.join(b'.' if p == stage else os.fsencode(p.relative_to(stage)) for p in paths) + b'\0'
        cpio = subprocess.run(['cpio', '--null', '-o', '--format=newc', '--owner=0:0'], cwd=stage,
                              input=names, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        with (images / filename).open('wb') as output:
            with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0, compresslevel=9) as compressed:
                compressed.write(cpio.stdout)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target', type=Path, required=True)
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--images', type=Path, required=True)
    p.add_argument('--readelf', required=True)
    p.add_argument('--epoch', type=int, default=0)
    a = p.parse_args()
    assemble(a.target, a.stage, a.images, a.readelf, a.epoch)


if __name__ == '__main__':
    main()
