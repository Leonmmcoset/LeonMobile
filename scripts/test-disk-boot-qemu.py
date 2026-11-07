#!/usr/bin/env python3
"""Exercise real EFI disk loading, menu selection and damaged-boot recovery."""
import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from PIL import Image

from leon_image import hash_region, partitions
from qemu_boot import boot_arguments

REPO = Path(__file__).resolve().parents[1]
ARTIFACTS = REPO / 'build/qemu-disk-boot-test'
spec = importlib.util.spec_from_file_location('leon_qemu_controls', REPO / 'scripts/test-qemu.py')
controls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(controls)


def wait_text(proc, log, marker, offset=0, timeout=180):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with log.open('rb') as stream:
            stream.seek(offset)
            text = stream.read().decode(errors='replace').replace('\r', '')
        if marker in text:
            return text
        if proc.poll() is not None:
            raise RuntimeError(f'QEMU exited waiting for {marker!r}; see {log}')
        time.sleep(.05)
    raise RuntimeError(f'Timed out waiting for {marker!r}; see {log}')


def screenshot(qmp, name, portrait=False):
    ppm = ARTIFACTS / (name + '.ppm')
    qmp.command('screendump', {'filename': str(ppm)})
    with Image.open(ppm) as frame:
        if portrait and frame.size != (480, 960):
            raise RuntimeError('Expected portrait display: ' + name)
        if len(frame.convert('RGB').getcolors(frame.width * frame.height) or []) < 2:
            raise RuntimeError('Blank display: ' + name)
        frame.save(ppm.with_suffix('.png'))


@contextmanager
def session(images, disk, tag, mode=None, recovery_media=False):
    endpoint = disk.parent / (tag + '-qmp.sock')
    log = ARTIFACTS / (tag + '-serial.log')
    command = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-smp', '4', '-m', '2048',
               '-drive', f'if=none,file={disk},format=raw,id=system',
               '-device', 'virtio-blk-pci,drive=system,bootindex=2',
               '-device', 'virtio-gpu-pci,xres=480,yres=960', '-device', 'qemu-xhci',
               '-device', 'usb-kbd', '-device', 'usb-tablet',
               '-netdev', 'user,id=net0', '-device', 'virtio-net-pci,netdev=net0',
               '-audiodev', 'none,id=audio', '-device', 'intel-hda',
               '-device', 'hda-duplex,audiodev=audio', '-display', 'none',
               '-monitor', 'none', '-serial', 'stdio', '-qmp', f'unix:{endpoint},server=on,wait=off']
    command += boot_arguments(images, disk, disk.with_suffix('.vars.fd'),
                              mode=mode, recovery_media=recovery_media)
    if any(arg in command for arg in ('-kernel', '-initrd', '-append')):
        raise RuntimeError('The disk boot test must never load host kernel/initramfs')
    (ARTIFACTS / (tag + '-command.json')).write_text(json.dumps(command, indent=2) + '\n')
    with log.open('wb') as output:
        proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT)
        qmp = None
        try:
            deadline = time.monotonic() + 15
            while not endpoint.exists() and time.monotonic() < deadline:
                if proc.poll() is not None:
                    raise RuntimeError('QEMU exited before QMP startup: ' + str(log))
                time.sleep(.05)
            qmp = controls.QMP(endpoint)
            yield proc, qmp, controls.Guest(proc, log), log
        except BaseException:
            if qmp:
                try:
                    screenshot(qmp, tag + '-failure')
                except (OSError, RuntimeError):
                    pass
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
            if qmp:
                qmp.stream.close()
                qmp.socket.close()


def healthy(proc, guest, log, offset=0, mode='normal'):
    wait_text(proc, log, 'leond: started shell', offset)
    wait_text(proc, log, 'LEON_CONSOLE_READY', offset)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        text = guest.run('cat /run/leon-boot/status')
        if 'healthy=true' in text:
            if 'mode=' + mode + '\n' not in text:
                raise RuntimeError('Wrong startup mode: ' + text)
            break
        time.sleep(.2)
    else:
        raise RuntimeError('Disk boot did not become healthy: ' + text)
    evidence = guest.run('test -d /sys/firmware/efi && cat /proc/cmdline; uname -r; id -u')
    if 'leon.boot=uefi' not in evidence or '7.3.1-leonmobile' not in evidence or \
            evidence.splitlines()[-1] != '1000':
        raise RuntimeError('Wrong firmware/kernel/console identity: ' + evidence)
    wall = int(guest.run('date +%s'))
    if abs(wall - time.time()) > 120:
        raise RuntimeError('EFI boot lost the host-backed RTC or system time: ' + str(wall))
    rtc = json.loads(guest.run('lmsys time \'{"op":"status"}\''))
    if not rtc.get('ok') or not rtc.get('rtc_available'):
        raise RuntimeError('The time service cannot read the EFI RTC: ' + repr(rtc))
    return evidence + '\n' + text


def stop_desktop(proc, guest):
    offset = guest.log.stat().st_size
    try:
        guest.run('lmsys power \'{"op":"poweroff"}\'')
    except RuntimeError:
        # PID 1 may finish shutdown before the serial shell prints its transport
        # footer. Require a clean QEMU exit and the kernel poweroff message.
        with guest.log.open('rb') as stream:
            stream.seek(offset)
            text = stream.read().decode(errors='replace')
        if proc.poll() != 0 or 'Power down' not in text:
            raise
    proc.wait(timeout=30)
    if proc.returncode:
        raise RuntimeError('Guest did not power off cleanly')


def clone(images, disk):
    subprocess.run(['cp', '--reflink=auto', '--sparse=always',
                    images / 'leonmobile-qemu-aarch64.img', disk], check=True)


def fat_offset(disk):
    with disk.open('rb') as stream:
        return partitions(stream)['boot'][0]


def fat_delete(disk, path):
    subprocess.run(['mdel', '-i', f'{disk}@@{fat_offset(disk)}', '::' + path], check=True)


def fat_put(disk, source, path):
    subprocess.run(['mcopy', '-o', '-i', f'{disk}@@{fat_offset(disk)}', source, '::' + path], check=True)


def wait_recovery(proc, qmp, log, tag):
    wait_text(proc, log, 'Independent recovery environment')
    wait_text(proc, log, 'Data partition: available; preserved')
    screenshot(qmp, tag, portrait=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--images', type=Path, default=REPO / 'output/images')
    a = parser.parse_args()
    images = a.images.resolve()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='leon-efi-test-') as name:
        temp = Path(name)
        disk = temp / 'menu.img'
        clone(images, disk)
        with session(images, disk, 'menu-safe') as (proc, qmp, guest, log):
            wait_text(proc, log, 'LEON_BOOT_MENU_READY')
            qmp.key('esc')
            wait_text(proc, log, 'Safe mode', timeout=15)
            screenshot(qmp, 'boot-menu')
            qmp.key('down')
            qmp.key('ret')
            evidence = healthy(proc, guest, log, mode='safe')
            (ARTIFACTS / 'safe-efi-status.txt').write_text(evidence)
            guest.run('printf disk-boot-data > /data/home/leon/disk-boot-witness')
            qmp.click(240, 790)
            time.sleep(1)
            screenshot(qmp, 'safe-desktop', portrait=True)
            old_id = guest.run('cat /proc/sys/kernel/random/boot_id')
            offset = log.stat().st_size
            guest.run('lmsys power \'{"op":"reboot"}\'')
            healthy(proc, guest, log, offset=offset)
            if guest.run('cat /proc/sys/kernel/random/boot_id') == old_id or \
                    guest.run('cat /data/home/leon/disk-boot-witness') != 'disk-boot-data':
                raise RuntimeError('Reboot did not retain data or restart the kernel')
            screenshot(qmp, 'normal-after-reboot', portrait=True)
            stop_desktop(proc, guest)
        print('PASS real EFI menu keyboard selection, safe desktop and normal kernel reboot with retained data', flush=True)

        # A request through the public launcher helper must be consumed once.
        with session(images, disk, 'requested-recovery', mode='recovery') as (proc, qmp, guest, log):
            wait_recovery(proc, qmp, log, 'requested-recovery')
            offset = log.stat().st_size
            proc.stdin.write(b'1\n')
            proc.stdin.flush()
            healthy(proc, guest, log, offset=offset)
            stop_desktop(proc, guest)
        print('PASS one-shot Recovery request and return through firmware to normal desktop', flush=True)

        legacy, migrated = temp / 'legacy.img', temp / 'migrated.img'
        subprocess.run(['cp', '--reflink=auto', '--sparse=always', disk, legacy], check=True)
        subprocess.run(['sfdisk', '--delete', legacy, '3'], check=True, capture_output=True)
        with legacy.open('rb') as stream:
            original = hash_region(stream, 0, legacy.stat().st_size)
        subprocess.run(['python3', REPO / 'scripts/migrate-boot.py', legacy, migrated,
                        '--images', images], check=True)
        with legacy.open('rb') as stream:
            if hash_region(stream, 0, legacy.stat().st_size) != original:
                raise RuntimeError('Migration modified its original disk')
        with session(images, migrated, 'migrated-disk') as (proc, qmp, guest, log):
            healthy(proc, guest, log)
            if guest.run('cat /data/home/leon/disk-boot-witness') != 'disk-boot-data':
                raise RuntimeError('Legacy migration lost user data')
            stop_desktop(proc, guest)
        print('PASS legacy two-partition migration, original disk unchanged and user file retained after EFI boot', flush=True)

        cases = ('missing-kernel', 'corrupt-initramfs', 'invalid-kernel-format', 'missing-menu')
        for tag in cases:
            disk = temp / (tag + '.img')
            clone(images, disk)
            if tag == 'missing-kernel':
                fat_delete(disk, '/EFI/LeonMobile/normal/Image')
            elif tag == 'corrupt-initramfs':
                bad = temp / 'bad-initramfs'
                bad.write_bytes(b'damaged initramfs')
                fat_put(disk, bad, '/EFI/LeonMobile/normal/initramfs')
            elif tag == 'invalid-kernel-format':
                bad = temp / 'bad-kernel'
                bad.write_bytes(b'not an EFI Linux image\n')
                fat_put(disk, bad, '/EFI/LeonMobile/normal/Image')
                # Keep the hashes correct, so this exercises the loader's error
                # path rather than merely repeating hash mismatch handling.
                archive = images / 'leon-initramfs.cpio.gz'
                manifest = temp / 'bad-kernel-hashes'
                manifest.write_text(hashlib.sha256(bad.read_bytes()).hexdigest() +
                                    '  /EFI/LeonMobile/normal/Image\n' +
                                    hashlib.sha256(archive.read_bytes()).hexdigest() +
                                    '  /EFI/LeonMobile/normal/initramfs\n')
                fat_put(disk, manifest, '/EFI/LeonMobile/normal/SHA256SUMS')
            else:
                fat_delete(disk, '/EFI/BOOT/grub.cfg')
            with session(images, disk, tag) as (proc, qmp, guest, log):
                wait_recovery(proc, qmp, log, tag)
                proc.stdin.write(b'7\n')
                proc.stdin.flush()
                proc.wait(timeout=20)
                if proc.returncode:
                    raise RuntimeError('Recovery poweroff failed')
            print('PASS ' + tag + ' -> independent recovery', flush=True)

        # Even total FAT destruction must not prevent USB recovery. Restore only
        # boot, assert system/data byte hashes, then boot the repaired disk.
        disk = temp / 'broken-esp.img'
        clone(images, disk)
        with disk.open('r+b') as stream:
            layout = partitions(stream)
            before = {key: hash_region(stream, *layout[key]) for key in ('system', 'data')}
            stream.seek(layout['boot'][0])
            stream.write(b'\0' * (64 * 1024))
        with session(images, disk, 'external-recovery', recovery_media=True) as (proc, qmp, guest, log):
            wait_recovery(proc, qmp, log, 'external-recovery')
            proc.stdin.write(b'7\n')
            proc.stdin.flush()
            proc.wait(timeout=20)
            if proc.returncode:
                raise RuntimeError('Standalone Recovery poweroff failed')
        # Recovery records its boot metadata on data. Snapshot it after that
        # deliberate change and ensure the offline boot writer changes no data.
        with disk.open('rb') as stream:
            if hash_region(stream, *layout['system']) != before['system']:
                raise RuntimeError('Standalone Recovery changed the system')
            before['data'] = hash_region(stream, *layout['data'])
        subprocess.run(['python3', REPO / 'scripts/repair-boot.py', disk, '--images', images], check=True)
        with disk.open('rb') as stream:
            if any(hash_region(stream, *layout[key]) != before[key] for key in before):
                raise RuntimeError('Boot repair modified system or data')
        with session(images, disk, 'repaired-boot') as (proc, qmp, guest, log):
            healthy(proc, guest, log)
            screenshot(qmp, 'repaired-desktop', portrait=True)
            stop_desktop(proc, guest)
        print('PASS destroyed EFI filesystem -> standalone USB recovery, verified boot-only repair and normal desktop', flush=True)
    print('EFI disk boot, startup selection, failure recovery and offline boot repair passed', flush=True)


if __name__ == '__main__':
    main()
