"""Shared GPT admission for LeonMobile's offline disk tools."""
import hashlib
import struct
import uuid
import zlib

LINUX_FILESYSTEM = uuid.UUID('0fc63daf-8483-4772-8e79-3d69d8477de4').bytes_le
EFI_SYSTEM = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le


def partitions(disk):
    disk.seek(0, 2)
    disk_size = disk.tell()
    disk.seek(512)
    header = disk.read(512)
    if header[:8] != b'EFI PART':
        raise ValueError('Disk has no GPT header')
    header_size = struct.unpack_from('<I', header, 12)[0]
    if not 92 <= header_size <= 512:
        raise ValueError('Invalid GPT header size')
    check = bytearray(header[:header_size])
    stored = struct.unpack_from('<I', check, 16)[0]
    struct.pack_into('<I', check, 16, 0)
    if zlib.crc32(check) != stored:
        raise ValueError('Corrupt GPT header')
    lba, count, size, crc = struct.unpack_from('<QIII', header, 72)
    if not 1 <= count <= 4096 or not 128 <= size <= 4096 or size % 8 or lba < 2:
        raise ValueError('Invalid GPT entry table')
    end = lba * 512 + count * size
    if end > disk_size:
        raise ValueError('GPT entry table exceeds disk')
    disk.seek(lba * 512)
    entries = disk.read(count * size)
    if len(entries) != count * size or zlib.crc32(entries) != crc:
        raise ValueError('Corrupt GPT entry table')
    result = {}
    previous_end = (end + 511) // 512
    for i in range(count):
        entry = entries[i * size:(i + 1) * size]
        if not any(entry[:16]):
            continue
        first, last = struct.unpack_from('<QQ', entry, 32)
        name = entry[56:128].decode('utf-16-le').rstrip('\0')
        expected = (('system', LINUX_FILESYSTEM), ('data', LINUX_FILESYSTEM), ('boot', EFI_SYSTEM))
        if i >= len(expected) or (name, entry[:16]) != expected[i]:
            raise ValueError('Expected system/data and optional EFI boot as partitions 1/2/3')
        if first < previous_end or last < first or (last + 34) * 512 > disk_size:
            raise ValueError('Partitions overlap or exceed disk size')
        previous_end = last + 1
        result[name] = (first * 512, (last - first + 1) * 512)
    if set(result) not in ({'system', 'data'}, {'system', 'data', 'boot'}):
        raise ValueError('Expected LeonMobile system/data and optional EFI boot partitions')
    return result


def grub_environment(mode=''):
    if mode not in ('', 'normal', 'safe', 'recovery'):
        raise ValueError('Unsupported boot mode')
    content = ('# GRUB Environment Block\nnext_entry=' + mode + '\n').encode('ascii')
    return content.ljust(1024, b'#')


def copy_region(source, target, size):
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        block = source.read(min(1024 * 1024, remaining))
        if not block:
            raise ValueError('Short partition read')
        target.write(block)
        digest.update(block)
        remaining -= len(block)
    return digest.hexdigest()


def hash_region(stream, offset, size):
    stream.seek(offset)
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        block = stream.read(min(1024 * 1024, remaining))
        if not block:
            raise ValueError('Short partition read')
        digest.update(block)
        remaining -= len(block)
    return digest.hexdigest()
