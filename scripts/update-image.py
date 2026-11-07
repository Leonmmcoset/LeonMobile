#!/usr/bin/env python3
"""Replace only the system partition of a stopped QEMU disk; preserve user data."""
import argparse
import fcntl
import os
from pathlib import Path
from leon_image import partitions


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
