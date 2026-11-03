#!/usr/bin/env python3
"""Replace only the system partition of a stopped QEMU disk; preserve user data."""
import argparse
import fcntl
import os
from pathlib import Path
import struct
import uuid
import zlib

LINUX_FILESYSTEM = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4").bytes_le


def partitions(disk):
    disk.seek(512)
    header = disk.read(512)
    if header[:8] != b"EFI PART":
        raise ValueError("Disk has no GPT header")
    header_size = struct.unpack_from("<I", header, 12)[0]
    if not 92 <= header_size <= 512:
        raise ValueError("Invalid GPT header size")
    check = bytearray(header[:header_size])
    stored = struct.unpack_from("<I", check, 16)[0]
    struct.pack_into("<I", check, 16, 0)
    if zlib.crc32(check) != stored:
        raise ValueError("Corrupt GPT header")
    lba, count, size, crc = struct.unpack_from("<QIII", header, 72)
    if count > 4096 or size < 128 or size > 4096:
        raise ValueError("Invalid GPT entry table")
    disk.seek(lba * 512)
    entries = disk.read(count * size)
    if len(entries) != count * size or zlib.crc32(entries) != crc:
        raise ValueError("Corrupt GPT entry table")
    result = {}
    for i in range(count):
        entry = entries[i * size:(i + 1) * size]
        if not any(entry[:16]):
            continue
        first, last = struct.unpack_from("<QQ", entry, 32)
        name = entry[56:128].decode("utf-16-le").rstrip("\0")
        if last < first or entry[:16] != LINUX_FILESYSTEM:
            raise ValueError("Unexpected partition layout")
        if name in result:
            raise ValueError("Duplicate partition name")
        result[name] = (first * 512, (last - first + 1) * 512)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("disk", type=Path)
    p.add_argument("system_image", type=Path)
    args = p.parse_args()
    if args.disk.resolve() == args.system_image.resolve():
        p.error("Disk and system image must be distinct files")
    with args.disk.open("r+b") as disk:
        fcntl.lockf(disk, fcntl.LOCK_EX | fcntl.LOCK_NB)
        layout = partitions(disk)
        if set(layout) != {"system", "data"}:
            p.error("Expected exactly LeonMobile system and data partitions")
        offset, capacity = layout["system"]
        data_offset, data_capacity = layout["data"]
        if offset + capacity > data_offset or data_offset + data_capacity > args.disk.stat().st_size:
            p.error("Partitions overlap or exceed disk size")
        if args.system_image.stat().st_size > capacity:
            p.error("New system image exceeds partition capacity")
        with args.system_image.open("rb") as source:
            source.seek(1024 + 56)
            if source.read(2) != b"\x53\xef":
                p.error("System image is not ext4")
            source.seek(0)
            disk.seek(offset)
            while block := source.read(1024 * 1024):
                disk.write(block)
        disk.flush()
        os.fsync(disk.fileno())
    print("System partition replaced; data partition preserved")


if __name__ == "__main__":
    main()
