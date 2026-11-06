#!/usr/bin/env python3
"""Exercise startup recovery and verified system restore on a disposable QEMU disk."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

from PIL import Image

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "build"
IMAGES = REPO / "output/images"
ARTIFACTS = BUILD / "qemu-recovery-test"
PIN = "739182"
ROOT_PASSWORD = "RecoveryTest!2026"
PRIMARY = bytes.fromhex("6750a4")
LOCK_BACKGROUND = bytes.fromhex("fff8ff")
COMMAND_LINE = (REPO / "distro/board/qemu-arm64/cmdline").read_text().strip()

spec = importlib.util.spec_from_file_location("leon_qemu_test", REPO / "scripts/test-qemu.py")
qemu_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu_test)
Guest, QMP = qemu_test.Guest, qemu_test.QMP


def launch_qemu(disk, repair, tag, initramfs=None):
    endpoint = ARTIFACTS / f"{tag}-qmp.sock"
    try:
        endpoint.unlink()
    except FileNotFoundError:
        pass
    log = ARTIFACTS / f"{tag}-serial.log"
    command = ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-smp", "4",
               "-m", "2048", "-kernel", str(IMAGES / "Image"), "-initrd",
               str(initramfs or IMAGES / "leon-initramfs.cpio.gz"), "-append", COMMAND_LINE,
               "-drive", f"if=none,file={disk},format=raw,id=system", "-device",
               "virtio-blk-pci,drive=system", "-drive",
               f"if=none,file={repair},format=raw,readonly=on,id=repair", "-device",
               "virtio-blk-pci,drive=repair", "-device", "virtio-gpu-pci,xres=480,yres=960",
               "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
               "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
               "-audiodev", "none,id=audio", "-device", "intel-hda", "-device",
               "hda-duplex,audiodev=audio", "-display", "none", "-monitor", "none",
               "-serial", "stdio", "-qmp", f"unix:{endpoint},server=on,wait=off"]
    output = log.open("wb")
    proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output,
                            stderr=subprocess.STDOUT)
    deadline = time.monotonic() + 20
    while not endpoint.exists() and time.monotonic() < deadline:
        if proc.poll() is not None:
            output.close()
            raise RuntimeError(f"QEMU exited before {tag} started; see {log}")
        time.sleep(.05)
    if not endpoint.exists():
        proc.terminate()
        proc.wait(timeout=10)
        output.close()
        raise RuntimeError(f"QMP socket did not appear for {tag}")
    qmp = QMP(endpoint)
    return proc, output, qmp, Guest(proc, log), log


def wait_log(log, offset, marker, timeout=90):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with log.open("rb") as stream:
            stream.seek(offset)
            text = stream.read().decode(errors="replace").replace("\r", "")
        if marker in text:
            return text
        time.sleep(.1)
    raise RuntimeError(f"Timed out waiting for serial marker {marker!r} in {log}")


def wait_guest_boot(guest, log, offset, timeout=120):
    wait_log(log, offset, "LeonMobile 1.0.0: init ready", timeout)
    deadline = time.monotonic() + timeout
    console_ready = False
    while time.monotonic() < deadline:
        with log.open("rb") as stream:
            stream.seek(offset)
            text = stream.read().decode(errors="replace").replace("\r", "")
        ready = text.find("LEON_CONSOLE_READY")
        prompt = text.find("$ ", ready + len("LEON_CONSOLE_READY")) if ready >= 0 else -1
        if ready >= 0 and prompt >= 0 and not console_ready:
            identity = guest.run("id -u").splitlines()
            if "1000" not in identity:
                raise RuntimeError("Serial development console identity was " + repr(identity))
            console_ready = True
        if console_ready and "leond: started shell" in text:
            return
        if guest.proc.poll() is not None:
            raise RuntimeError("QEMU exited during guest reboot")
        time.sleep(.15)
    details = guest.run("leonctl status") if console_ready else "console unavailable"
    raise RuntimeError("Guest reboot did not start the desktop:\n" + details)


def wait_initial_console(log, timeout=60):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = log.read_text(errors="replace").replace("\r", "")
        marker = text.find("LEON_CONSOLE_READY")
        if marker >= 0 and "$ " in text[marker:]:
            return
        time.sleep(.1)
    raise RuntimeError("Guest serial login did not reach the leon shell prompt")


def wait_phase(guest, phase, timeout=60):
    marker = f"RECOVERY_TESTS_PASSED phase={phase}"
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = guest.run("cat /run/leon/recovery-tests.txt 2>/dev/null || true")
        if "RECOVERY_TESTS_FAILED" in last or "FAIL recovery:" in last:
            raise RuntimeError("Recovery target fixture failed:\n" + last)
        if marker in last:
            return
        time.sleep(.2)
    raise RuntimeError(f"Recovery target fixture phase {phase} timed out:\n{last}")


def wait_fixture_line(guest, expected, timeout=60):
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = guest.run("cat /run/leon/recovery-tests.txt 2>/dev/null || true")
        if "RECOVERY_TESTS_FAILED" in last:
            raise RuntimeError("Recovery target fixture failed:\n" + last)
        if expected in last:
            return last
        time.sleep(.2)
    raise RuntimeError(f"Recovery target fixture did not reach {expected!r}:\n{last}")


def wait_healthy(guest, timeout=90):
    deadline = time.monotonic() + timeout
    status = ""
    while time.monotonic() < deadline:
        status = guest.run("cat /run/leon-boot/status")
        if "healthy=true" in status:
            return status
        time.sleep(.2)
    raise RuntimeError("Startup services and presented desktop did not become healthy:\n" + status)


def display_pixel(frame, x, y):
    start = (y * 480 + x) * 3
    return frame[start:start + 3]


def screenshot(qmp, path):
    ppm = path.with_suffix(".ppm")
    pixels = qmp.capture(ppm)
    with Image.open(ppm) as image:
        image.convert("RGB").save(path)
    if len(pixels) != 480 * 960 * 3:
        raise RuntimeError("QEMU screenshot did not contain a full portrait frame")
    print(f"Screenshot: {path}", flush=True)
    return pixels


def recovery_screenshot(qmp, path):
    ppm = path.with_suffix(".ppm")
    pixels = qmp.capture(ppm, allow_blank=True)
    # The kernel text console uses two flat palette colors, unlike LMUI's
    # antialiased desktop; a 16-color desktop heuristic rejects valid text.
    if len({pixels[i:i + 3] for i in range(0, len(pixels), 3)}) < 2:
        raise RuntimeError("Recovery text console is blank")
    with Image.open(ppm) as image:
        image.convert("RGB").save(path)
    print(f"Screenshot: {path}", flush=True)
    return pixels


def wait_screen(qmp, path, predicate, description, timeout=30):
    ppm = path.with_suffix(".ppm")
    pixels = qmp.wait_frame(ppm, predicate, description, timeout)
    with Image.open(ppm) as image:
        image.convert("RGB").save(path)
    print(f"Screenshot: {path}", flush=True)
    return pixels


def unlock_pin(qmp, guest, path):
    qmp.drag(240, 790, 240, 300)
    wait_screen(qmp, ARTIFACTS / "device-pin-entry.png",
                lambda frame: display_pixel(frame, 240, 285) == bytes.fromhex("eaddff") and
                              display_pixel(frame, 240, 370) == bytes.fromhex("fff8ff"),
                "Device PIN entry screen did not appear")
    for digit in PIN:
        number = int(digit)
        if number == 0:
            x, y = 236, 685
        else:
            x = round(56 + ((number - 1) % 3) * (480 - 112) / 3 + (480 - 136) / 6)
            y = round(406 + ((number - 1) // 3) * 82 + 33)
        qmp.click(x, y)
    qmp.click(359, 685)
    return wait_screen(qmp, path,
                       lambda frame: display_pixel(frame, 240, 210) == PRIMARY,
                       "Device PIN did not unlock the recovered desktop")


def setup_device_pin(qmp, guest):
    wait_screen(qmp, ARTIFACTS / "initial-lock-screen.png",
                lambda frame: display_pixel(frame, 0, 100) == LOCK_BACKGROUND and
                              display_pixel(frame, 240, 200) == bytes.fromhex("eaddff"),
                "Initial lock screen did not render")
    qmp.drag(240, 790, 240, 300)
    wait_screen(qmp, ARTIFACTS / "initial-unlocked-home.png",
                lambda frame: display_pixel(frame, 240, 210) == PRIMARY,
                "Unconfigured device did not allow initial session access")
    qmp.click(400, 22)
    time.sleep(.4)
    screenshot(qmp, ARTIFACTS / "setup-control-shade.png")
    qmp.click(372, 628)
    qmp.wait_frame(ARTIFACTS / "recovery-settings-home.ppm",
                   lambda frame: display_pixel(frame, 80, 196) == bytes.fromhex("eaddff"),
                   "Settings home did not render for device PIN setup")
    time.sleep(.4)
    qmp.click(180, 632)
    time.sleep(.4)
    qmp.click(180, 496)
    time.sleep(.4)
    screenshot(qmp, ARTIFACTS / "recovery-device-security.png")
    qmp.click(180, 370)
    time.sleep(.3)
    screenshot(qmp, ARTIFACTS / "recovery-pin-setup.png")
    qmp.type_text(PIN)
    qmp.key("ret")
    time.sleep(.4)
    screenshot(qmp, ARTIFACTS / "recovery-pin-confirm.png")
    qmp.click(180, 356)
    qmp.type_text(PIN)
    screenshot(qmp, ARTIFACTS / "recovery-pin-confirm-filled.png")
    qmp.click(350, 430)
    screenshot(qmp, ARTIFACTS / "recovery-pin-confirm-submitted.png")
    wait_fixture_line(guest, "RECOVERY_TESTS_PASSED phase=setup")
    guest.run("lmsys power '{\"op\":\"lock\"}'")
    qmp.wait_frame(ARTIFACTS / "recovery-device-lock.ppm",
                   lambda frame: display_pixel(frame, 0, 100) == LOCK_BACKGROUND and
                                 display_pixel(frame, 240, 200) == bytes.fromhex("eaddff"),
                   "Configured device PIN did not lock the session")
    unlock_pin(qmp, guest, ARTIFACTS / "initial-user-session.png")
    print("Target Settings: device PIN configured through Settings and verified by real lock/unlock", flush=True)


def trigger_reboot(guest, qmp, log, mode, current_boot):
    guest.run(f"rm -f /run/leon/recovery-trigger.result; printf '{mode}\\n' > /run/leon/recovery-trigger")
    deadline = time.monotonic() + 12
    result = ""
    while time.monotonic() < deadline:
        result = guest.run("cat /run/leon/recovery-trigger.result 2>/dev/null || true")
        if result == f"accepted {mode}":
            break
        if result.startswith("failed "):
            raise RuntimeError("Recovery trigger failed: " + result)
        time.sleep(.1)
    else:
        raise RuntimeError("Recovery trigger was not accepted: " + result)
    offset = log.stat().st_size
    if mode == "damage":
        wait_log(log, offset, "Independent recovery environment", 120)
        return current_boot
    else:
        wait_guest_boot(guest, log, offset, 120)
    new_boot = guest.run("cat /proc/sys/kernel/random/boot_id")
    if new_boot == current_boot:
        raise RuntimeError("Kernel boot ID did not change across the requested reboot")
    return new_boot


def wait_recovery(qmp, log, offset, path, reason="essential system files missing"):
    text = wait_log(log, offset, "Independent recovery environment", 60)
    if reason not in text:
        raise RuntimeError(f"Recovery did not report expected reason {reason!r}")
    wait_log(log, offset, "USB HID v1.11 Keyboard", 20)
    recovery_screenshot(qmp, path)


def choose(qmp, value):
    qmp.type_text(value)
    qmp.key("ret")


def stop_qemu(proc, output, qmp):
    if proc.poll() is None:
        try:
            qmp.key("7")
            qmp.key("ret")
            proc.wait(timeout=30)
        except (subprocess.TimeoutExpired, RuntimeError, OSError):
            if proc.poll() is None:
                try:
                    qmp.command("quit")
                except (RuntimeError, OSError, socket.error):
                    proc.terminate()
                proc.wait(timeout=15)
    try:
        qmp.socket.close()
    except OSError:
        pass
    output.close()


def preserve_failed_disk(disk, phase):
    if os.environ.get("LEON_KEEP_RECOVERY_TEST_DISK") != "1":
        return
    saved = ARTIFACTS / f"failed-{phase}-disk.img"
    subprocess.run(["cp", "--reflink=auto", "--sparse=always", disk, saved], check=True)
    print(f"Preserved failed-test disk: {saved}", flush=True)


def disk_partition(disk, number):
    table = json.loads(subprocess.check_output(["sfdisk", "--json", disk]))["partitiontable"]
    partition = table["partitions"][number - 1]
    sector = table.get("sectorsize", 512)
    return partition["start"] * sector, partition["size"] * sector


def hash_partition(disk, number):
    offset, size = disk_partition(disk, number)
    digest = hashlib.sha256()
    with disk.open("rb") as stream:
        stream.seek(offset)
        remaining = size
        while remaining:
            block = stream.read(min(1024 * 1024, remaining))
            if not block:
                raise RuntimeError("Short system partition read")
            digest.update(block)
            remaining -= len(block)
    return digest.hexdigest()


def extract_data(disk, temp):
    offset, size = disk_partition(disk, 2)
    image = temp / "verified-data.ext4"
    with disk.open("rb") as source, image.open("wb") as destination:
        source.seek(offset)
        remaining = size
        while remaining:
            block = source.read(min(1024 * 1024, remaining))
            if not block:
                raise RuntimeError("Short data partition read")
            destination.write(block)
            remaining -= len(block)
    return image


def debugfs(image, command):
    return subprocess.check_output(["debugfs", "-R", command, image],
                                   stderr=subprocess.DEVNULL, text=True)


def run_target_test(probe, package):
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="leon-recovery-qemu-") as temp_name:
        temp = Path(temp_name)
        disk = temp / "recovery-test.img"
        bad_repair = temp / "recovery-bad-source.ext4"
        subprocess.run(["cp", "--reflink=auto", "--sparse=always",
                        IMAGES / "leonmobile-qemu-aarch64.img", disk], check=True)
        subprocess.run(["cp", "--reflink=auto", "--sparse=always",
                        IMAGES / "rootfs.ext4", bad_repair], check=True)
        with bad_repair.open("r+b") as stream:
            stream.seek(-1, 2)
            value = stream.read(1)
            stream.seek(-1, 2)
            stream.write(bytes((value[0] ^ 0x80,)))
            stream.flush()
            import os
            os.fsync(stream.fileno())

        inject = temp / "inject-recovery-probe"
        qemu_test.inject_probes(IMAGES, disk, temp, None, None,
                                recovery_probe=probe, recovery_package=package)
        proc, output, qmp, guest, log = launch_qemu(disk, bad_repair, "recovery-bad")
        try:
            start = time.monotonic() + 180
            while time.monotonic() < start:
                if proc.poll() is not None:
                    raise RuntimeError("QEMU exited before the Recovery fixture started")
                try:
                    if "LEON_CONSOLE_READY" in log.read_text(errors="replace"):
                        break
                except OSError:
                    pass
                time.sleep(.2)
            else:
                raise RuntimeError("Guest serial development console did not start")
            wait_initial_console(log)
            identity = guest.run("id -u").splitlines()
            if "1000" not in identity:
                raise RuntimeError("Disposable QEMU console identity was " + repr(identity))
            wait_fixture_line(guest, "RECOVERY_TESTS_WAITING phase=device-pin")
            boot_id = guest.run("cat /proc/sys/kernel/random/boot_id")
            setup_device_pin(qmp, guest)
            boot_id = trigger_reboot(guest, qmp, log, "safe", boot_id)
            wait_phase(guest, "safe")
            status = wait_healthy(guest)
            if "mode=safe" not in status or "healthy=true" not in status:
                raise RuntimeError("Safe boot did not reach health-checked desktop readiness: " + status)
            unlock_pin(qmp, guest, ARTIFACTS / "safe-mode-home.png")
            boot_id = trigger_reboot(guest, qmp, log, "normal", boot_id)
            wait_phase(guest, "normal")
            status = wait_healthy(guest)
            if "mode=normal" not in status:
                raise RuntimeError("Normal retry did not consume the one-shot safe mode: " + status)
            qmp.click(240, 790)
            qmp.type_text(PIN)
            qmp.key("ret")
            time.sleep(1)
            screenshot(qmp, ARTIFACTS / "normal-boot-before-failure.png")
            system_before_cancel = None
            boot_offset = log.stat().st_size
            boot_id = trigger_reboot(guest, qmp, log, "damage", boot_id)
            wait_recovery(qmp, log, boot_offset, ARTIFACTS / "recovery-menu.png")
            choose(qmp, "3")
            wait_log(log, boot_offset, "Filesystem check passed (read only).", 60)
            recovery_screenshot(qmp, ARTIFACTS / "recovery-filesystem-check.png")

            choose(qmp, "4")
            wait_log(log, boot_offset, "Type RESTORE to replace system; data is retained", 10)
            recovery_screenshot(qmp, ARTIFACTS / "restore-confirmation.png")
            choose(qmp, "CANCEL")
            wait_log(log, boot_offset, "Restore cancelled; no system write.", 10)
            recovery_screenshot(qmp, ARTIFACTS / "restore-cancelled.png")
            system_before_cancel = hash_partition(disk, 1)

            choose(qmp, "4")
            wait_log(log, boot_offset, "Type RESTORE to replace system; data is retained", 10)
            choose(qmp, "RESTORE")
            wait_log(log, boot_offset, "Restore refused/failed. Data retained; see diagnostics.", 90)
            system_after_reject = hash_partition(disk, 1)
            if system_after_reject != system_before_cancel:
                raise RuntimeError("Invalid repair media modified the system partition")
            recovery_screenshot(qmp, ARTIFACTS / "restore-invalid-image-rejected.png")
            choose(qmp, "6")
            wait_log(log, boot_offset, "Enter Root password (hidden); Esc cancels", 10)
            qmp.type_text(ROOT_PASSWORD)
            qmp.key("ret")
            wait_log(log, boot_offset, "Saved /data/leon/boot/recovery-report.txt (Root only).", 30)
            recovery_screenshot(qmp, ARTIFACTS / "recovery-diagnostics-exported.png")
            stop_qemu(proc, output, qmp)

        except Exception:
            try:
                report = guest.run("cat /run/leon/recovery-tests.txt 2>/dev/null || true", timeout=5)
                status = guest.run("cat /run/leon/recovery-auth-status 2>/dev/null || true", timeout=5)
                if report:
                    (ARTIFACTS / "recovery-target-failure.txt").write_text(
                        report + "\nAUTH STATUS\n" + status + "\n")
            except Exception:
                pass
            if proc.poll() is None:
                try:
                    qmp.command("quit")
                except Exception:
                    proc.terminate()
                proc.wait(timeout=15)
            if not output.closed:
                output.close()
            try:
                qmp.socket.close()
            except OSError:
                pass
            preserve_failed_disk(disk, "boot")
            raise

        proc, output, qmp, guest, log = launch_qemu(
            disk, IMAGES / "rootfs.ext4", "recovery-valid", IMAGES / "leon-recovery.cpio.gz")
        try:
            wait_recovery(qmp, log, 0, ARTIFACTS / "recovery-ready-to-restore.png")
            choose(qmp, "4")
            wait_log(log, 0, "Type RESTORE to replace system; data is retained", 10)
            recovery_screenshot(qmp, ARTIFACTS / "restore-valid-confirmation.png")
            choose(qmp, "RESTORE")
            wait_log(log, 0, "Verified source; writing system only", 180)
            recovery_screenshot(qmp, ARTIFACTS / "restore-writing.png")
            wait_log(log, 0, "System restored and read-back verified. Data retained.", 300)
            recovery_screenshot(qmp, ARTIFACTS / "restore-complete.png")
            offset = log.stat().st_size
            choose(qmp, "1")
            wait_guest_boot(guest, log, offset, 180)
            status = wait_healthy(guest, 90)
            if "mode=normal" not in status:
                raise RuntimeError("Restored system did not return to normal mode: " + status)
            root = json.loads(guest.run("su --status"))
            if not root.get("enabled") or not root.get("password_set"):
                raise RuntimeError("Root policy did not survive system restore: " + str(root))
            if "org.example.hello" not in guest.run("lmpkg list"):
                raise RuntimeError("Installed third-party package did not survive restore")
            if guest.run("cat /data/home/leon/recovery-witness") != "LeonMobile recovery preserves user data":
                raise RuntimeError("User home data did not survive system restore")
            unlock_pin(qmp, guest, ARTIFACTS / "restored-home.png")
            # Select the installed Hello icon through the shell. A CLI launch
            # starts the process but does not dismiss the shell's home scene.
            qmp.click(96, 408)
            guest.wait_lifecycle("org.example.hello")
            wait_screen(qmp, ARTIFACTS / "restored-hello-app.png",
                        lambda frame: display_pixel(frame, 40, 286) == PRIMARY and
                                      display_pixel(frame, 240, 330) == LOCK_BACKGROUND,
                        "Installed application did not render after system restore")
            guest.run("lmsys power '{\"op\":\"poweroff\"}'")
            proc.wait(timeout=30)
            if proc.returncode:
                raise RuntimeError(f"Restored system did not power off cleanly ({proc.returncode})")
            output.close()
            qmp.socket.close()
        except Exception:
            if proc.poll() is None:
                try:
                    qmp.command("quit")
                except Exception:
                    proc.terminate()
                proc.wait(timeout=15)
            if not output.closed:
                output.close()
            try:
                qmp.socket.close()
            except OSError:
                pass
            preserve_failed_disk(disk, "restore")
            raise

        if hash_partition(disk, 1) != hashlib.sha256((IMAGES / "rootfs.ext4").read_bytes()).hexdigest():
            raise RuntimeError("Restored system partition differs from its trusted source image")
        data_image = extract_data(disk, temp)
        root_policy = json.loads(debugfs(data_image, "cat /leon/rootd/policy.json"))
        auth_policy = json.loads(debugfs(data_image, "cat /leon/authd/policy.json"))
        witness = debugfs(data_image, "cat /leon/data/org.example.hello/recovery-witness")
        recovery_report = debugfs(data_image, "cat /leon/boot/recovery-report.txt")
        if not root_policy.get("enabled") or auth_policy.get("kind") != "pin" or \
                "installed app data survives system restore" not in witness or \
                "LeonMobile Recovery 1.0.0" not in recovery_report or \
                "reason=essential system files missing" not in recovery_report:
            raise RuntimeError("Offline data verification found lost Root, PIN, or app-private state")
        apps = debugfs(data_image, "ls -l /leon/apps")
        if "org.example.hello" not in apps:
            raise RuntimeError("Offline package database did not preserve the third-party app")

        proc, output, qmp, guest, log = launch_qemu(
            disk, IMAGES / "rootfs.ext4", "standalone-recovery", IMAGES / "leon-recovery.cpio.gz")
        try:
            wait_log(log, 0, "recovery requested", 60)
            wait_recovery(qmp, log, 0, ARTIFACTS / "standalone-recovery-menu.png", "recovery requested")
            offset = log.stat().st_size
            choose(qmp, "1")
            wait_guest_boot(guest, log, offset, 180)
            status = wait_healthy(guest, 90)
            if "mode=normal" not in status:
                raise RuntimeError("Standalone Recovery did not honor its normal-boot request: " + status)
            unlock_pin(qmp, guest, ARTIFACTS / "standalone-recovery-return.png")
            guest.run("lmsys power '{\"op\":\"poweroff\"}'")
            proc.wait(timeout=30)
            if proc.returncode:
                raise RuntimeError("Standalone Recovery return boot did not power off cleanly")
            output.close()
            qmp.socket.close()
        except Exception:
            if proc.poll() is None:
                try:
                    qmp.command("quit")
                except Exception:
                    proc.terminate()
                proc.wait(timeout=15)
            if not output.closed:
                output.close()
            try:
                qmp.socket.close()
            except OSError:
                pass
            preserve_failed_disk(disk, "standalone-recovery")
            raise
    print("Recovery QEMU: safe/normal boot, missing-file entry, read-only fsck, cancel, invalid source rejection, verified restore, Root/PIN/app/user data persistence passed", flush=True)


def main():
    probe = BUILD / "recovery-target-probe"
    package = BUILD / "hello/org.example.hello-1.0.0.lmpkg"
    if not (IMAGES / "leonmobile-qemu-aarch64.img").is_file() or not (IMAGES / "leon-recovery.cpio.gz").is_file():
        raise SystemExit("Build the full QEMU image first: ./scripts/build.sh")
    if not probe.is_file() or not package.is_file():
        raise SystemExit("Build target fixtures first: ./scripts/build-target-probes.sh && ./scripts/build-example.sh")
    run_target_test(probe, package)


if __name__ == "__main__":
    main()
