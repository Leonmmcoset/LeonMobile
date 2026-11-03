#!/usr/bin/env python3
"""Boot the reference image, capture the real display and test mobile input."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shlex
import socket
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]


class Guest:
    def __init__(self, proc, log):
        self.proc, self.log, self.sequence = proc, log, 0

    def run(self, command, timeout=15):
        self.sequence += 1
        marker = f"LEON_CHECK_{self.sequence}"
        start = self.log.stat().st_size
        line = (f"printf '\\n{marker}\\n'; ( {command} ); "
                f"result=$?; printf '\\n{marker}_END:%s\\n' \"$result\"\n")
        self.proc.stdin.write(line.encode())
        self.proc.stdin.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self.log.open("rb") as stream:
                stream.seek(start)
                text = stream.read().decode(errors="replace").replace("\r", "")
            match = re.search(rf"\n{marker}\n(.*?)\n{marker}_END:(\d+)\n", text, re.S)
            if match:
                if match[2] != "0":
                    raise RuntimeError(f"Guest command failed: {command}\n{match[1]}")
                return match[1].strip()
            if self.proc.poll() is not None:
                raise RuntimeError("QEMU exited during guest command: " + command)
            time.sleep(.1)
        raise RuntimeError("Guest command timed out: " + command)

    def upload(self, path, destination):
        """Transfer small test payloads through the non-root development console."""
        encoded = base64.b64encode(path.read_bytes()).decode()
        target = shlex.quote(destination)
        self.run(f": > {target}.b64")
        # BusyBox ash's line editor has a bounded input buffer. Keep each
        # complete command well below its 1024-byte limit.
        for offset in range(0, len(encoded), 512):
            self.run(f"printf '%s' '{encoded[offset:offset + 512]}' >> {target}.b64")
        self.run(f"base64 -d {target}.b64 > {target} && rm {target}.b64")
        digest = self.run(f"sha256sum {target}").split()[0]
        if digest != hashlib.sha256(path.read_bytes()).hexdigest():
            raise RuntimeError("Guest payload transfer checksum mismatch")

    def wait_setting(self, key, expected):
        deadline = time.monotonic() + 10
        actual = None
        while time.monotonic() < deadline:
            actual = json.loads(self.run("cat /data/leon/settings.json")).get(key)
            if actual == expected:
                return
            time.sleep(.2)
        raise RuntimeError(f"Setting {key} did not reach {expected!r}; observed {actual!r}")


class QMP:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(10)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb", buffering=0)
        self.read()
        self.command("qmp_capabilities")

    def read(self):
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QEMU closed QMP")
            message = json.loads(line)
            if "event" not in message:
                return message

    def command(self, command, arguments=None):
        self.stream.write(json.dumps({"execute": command,
                                      "arguments": arguments or {}}).encode() + b"\n")
        response = self.read()
        if "error" in response:
            raise RuntimeError(response["error"])
        return response.get("return")

    def move(self, x, y):
        self.command("input-send-event", {"events": [
            {"type": "abs", "data": {"axis": "x", "value": int(x/480*32767)}},
            {"type": "abs", "data": {"axis": "y", "value": int(y/960*32767)}}]})

    def button(self, down):
        self.command("input-send-event", {"events": [
            {"type": "btn", "data": {"down": down, "button": "left"}}]})

    def click(self, x, y):
        self.move(x, y)
        time.sleep(.05)
        self.button(True)
        time.sleep(.1)
        self.button(False)

    def drag(self, x1, y1, x2, y2, held_frame=None):
        self.move(x1, y1)
        time.sleep(.05)
        self.button(True)
        for step in range(1, 13):
            self.move(x1 + (x2 - x1) * step / 12, y1 + (y2 - y1) * step / 12)
            time.sleep(.06)
        time.sleep(.3)
        frame = self.capture(held_frame) if held_frame else None
        self.button(False)
        time.sleep(.5)
        return frame

    def key(self, code):
        for down in (True, False):
            self.command("input-send-event", {"events": [
                {"type": "key", "data": {"down": down,
                 "key": {"type": "qcode", "data": code}}}]})
            time.sleep(.05)

    def capture(self, path):
        self.command("screendump", {"filename": str(path)})
        with path.open("rb") as stream:
            if stream.readline().strip() != b"P6":
                raise RuntimeError("QEMU did not produce a PPM display frame")
            size = stream.readline().strip()
            maximum = stream.readline().strip()
            pixels = stream.read()
        if size != b"480 960" or maximum != b"255" or len(pixels) != 480 * 960 * 3:
            raise RuntimeError("Expected a complete 480x960 portrait display")
        if len({pixels[i:i + 3] for i in range(0, len(pixels), 48)}) < 16:
            raise RuntimeError("QEMU display is blank")
        return pixels

    def wait_frame(self, path, predicate, description):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            frame = self.capture(path)
            if predicate(frame):
                return frame
            time.sleep(.15)
        raise RuntimeError(description)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=int, default=150)
    parser.add_argument("--images", type=Path, default=REPO / "output/images",
                        help="image directory, including an assembled local release")
    parser.add_argument("--package", type=Path, help="also install, launch and remove the SDK hello package")
    parser.add_argument("--notification-probe", type=Path,
                        help="AArch64 tests/target/notifications.c executable for live notification checks")
    args = parser.parse_args()
    if args.package and not args.package.is_file():
        parser.error("--package must name a compiled SDK hello package")
    if args.notification_probe and not args.notification_probe.is_file():
        parser.error("--notification-probe must name a compiled AArch64 probe")
    images = args.images.resolve()
    artifacts = REPO / "build/qemu-test"
    artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="leon-qemu-") as temp:
        temp = Path(temp)
        disk = temp / "disk.img"
        subprocess.run(["cp", "--reflink=auto", "--sparse=always",
                        images / "leonmobile-qemu-aarch64.img", disk], check=True)
        endpoint = temp / "qmp.sock"
        command = ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-smp", "4",
                   "-m", "2048", "-kernel", str(images / "Image"), "-initrd",
                   str(images / "leon-initramfs.cpio.gz"), "-append",
                   "console=ttyAMA0 leon.root=/dev/vda1 rootwait ro", "-drive",
                   f"if=none,file={disk},format=raw,id=system", "-device",
                   "virtio-blk-pci,drive=system", "-device", "virtio-gpu-pci,xres=480,yres=960",
                   "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
                   "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
                   "-audiodev", "none,id=audio", "-device", "intel-hda", "-device",
                   "hda-duplex,audiodev=audio", "-display", "none", "-monitor", "none",
                   "-serial", "stdio", "-qmp", f"unix:{endpoint},server=on,wait=off"]
        log = artifacts / "serial.log"
        with log.open("wb") as output:
            proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output,
                                    stderr=subprocess.STDOUT)
            qmp = None
            passed = False
            try:
                deadline = time.monotonic() + args.timeout
                while not endpoint.exists() and time.monotonic() < deadline:
                    time.sleep(.05)
                qmp = QMP(endpoint)
                while time.monotonic() < deadline:
                    text = log.read_text(errors="replace")
                    if proc.poll() is not None:
                        raise RuntimeError("QEMU exited before startup; see " + str(log))
                    if "leond: started shell" in text and "\n$ " in text.replace("\r", ""):
                        break
                    time.sleep(.25)
                else:
                    raise RuntimeError("Boot timed out; see " + str(log))
                # Serial login is non-root. The broker provides controlled operations.
                guest = Guest(proc, log)
                if guest.run("id -u") != "1000":
                    raise RuntimeError("Development console must be non-root")
                if guest.run("uname -r") != "7.3.1-leonmobile":
                    raise RuntimeError("Guest kernel version mismatch")
                apps = json.loads(guest.run("lmpkg list"))
                if len(apps) != 8 or any(app["arch"] != "aarch64" for app in apps):
                    raise RuntimeError("Native system application manifest checks failed")
                entries = {app["entry"] for app in apps}
                expected = {f"/usr/bin/leon-{name}" for name in (
                    "settings", "calculator", "gallery", "files", "editor",
                    "terminal", "images", "clock")}
                if entries != expected or any(app.get("args") for app in apps):
                    raise RuntimeError("Built-in apps must have independent ELF entry points")
                guest.run(" && ".join(f"test -x {shlex.quote(entry)}" for entry in sorted(entries)))
                guest.run("test ! -e /usr/bin/leon-app")
                while time.monotonic() < deadline:
                    try:
                        frame = qmp.capture(artifacts / "lock.ppm")
                        # Wait for the initial fade to finish before recording
                        # the lock screen or sending the first pointer event.
                        point = (100 * 480) * 3
                        if frame[point:point + 3] == bytes.fromhex("fff8ff"):
                            break
                    except RuntimeError:
                        pass
                    time.sleep(.25)
                else:
                    raise RuntimeError("Desktop did not render; inspect guest compositor logs")
                qmp.click(240, 790)
                time.sleep(1)
                qmp.capture(artifacts / "home.ppm")
                notification_terminal = None
                if args.notification_probe:
                    guest.upload(args.notification_probe, "/tmp/shade-notify")
                    guest.run("chmod 755 /tmp/shade-notify")
                    qmp.click(240, 838)  # All apps
                    time.sleep(.5)
                    qmp.click(240, 426)  # Terminal
                    for attempt in range(40):
                        terminal = qmp.capture(artifacts / "shade-notification-terminal.ppm")
                        point = (180 * 480 + 24) * 3
                        if terminal[point:point + 3] == bytes.fromhex("19151f"):
                            break
                        time.sleep(.25)
                    else:
                        raise RuntimeError("Terminal did not render for notification probe")
                    for code in "/tmp/shade-notify":
                        qmp.key({"/": "slash", "-": "minus"}.get(code, code))
                    qmp.key("ret")
                    time.sleep(1)
                    result = guest.run("cat /tmp/shade-notify.result")
                    if "12 notifications submitted" not in result:
                        raise RuntimeError("Notification probe failed")
                    notification_terminal = qmp.capture(artifacts / "shade-notification-terminal.ppm")
                    qmp.click(240, 940)
                    time.sleep(.5)
                qmp.click(96, 410)
                time.sleep(2)
                qmp.capture(artifacts / "calculator.ppm")
                running = json.loads(guest.run("lmpkg running"))
                if "org.leon.calculator" not in running:
                    raise RuntimeError("Calculator did not launch from the home screen")
                for x, y in ((72, 384), (408, 556), (296, 556), (408, 642)):
                    qmp.click(x, y)
                    time.sleep(.1)
                time.sleep(.3)
                calculator = qmp.capture(artifacts / "calculator-result.ppm")
                calculator_pid = running["org.leon.calculator"]
                # Hold the pointer across the status/app input boundary. The
                # shade must follow before release, then snap to stage 1.
                held = qmp.drag(240, 20, 240, 300, artifacts / "shade-dragging.ppm")
                compact = qmp.capture(artifacts / "shade-compact.ppm")
                point = (380 * 480 + 8) * 3
                if (held[point:point + 3] == bytes.fromhex("fff8ff") or
                        compact[point:point + 3] != bytes.fromhex("fff8ff")):
                    raise RuntimeError("Shade did not follow the drag and settle to compact")
                qmp.drag(240, 110, 240, 450)
                expanded = qmp.capture(artifacts / "shade-expanded.ppm")
                point = (820 * 480 + 8) * 3
                if (compact[point:point + 3] == bytes.fromhex("fff8ff") or
                        expanded[point:point + 3] != bytes.fromhex("fff8ff")):
                    raise RuntimeError("Second pull did not expand the shade")
                if notification_terminal:
                    qmp.drag(280, 726, 280, 520)
                    scrolled = qmp.capture(artifacts / "shade-notifications-scrolled.ppm")
                    def notification_region(frame):
                        return b"".join(frame[(y * 480 + 40) * 3:(y * 480 + 420) * 3]
                                        for y in range(520, 760))
                    if notification_region(expanded) == notification_region(scrolled):
                        raise RuntimeError("Notification drag did not scroll the list")
                    qmp.drag(280, 520, 280, 800)
                    qmp.click(180, 540)
                    time.sleep(1)
                    opened = qmp.capture(artifacts / "shade-notification-open.ppm")
                    def app_header(frame):
                        return b"".join(frame[(y * 480 + 24) * 3:(y * 480 + 350) * 3]
                                        for y in range(60, 110))
                    if app_header(opened) != app_header(notification_terminal):
                        raise RuntimeError("Notification tap did not open its owning app")
                    print("Target notifications: 12 live notifications, drag scroll and app activation passed")
                    qmp.click(240, 940)
                    time.sleep(.5)
                    qmp.click(96, 410)
                    time.sleep(.5)
                    qmp.drag(240, 20, 240, 300)
                    qmp.drag(240, 110, 240, 450)
                qmp.click(350, 200)  # Theme tile
                time.sleep(.5)
                if not json.loads(guest.run("cat /data/leon/settings.json"))["dark"]:
                    raise RuntimeError("Shade theme tile did not update system settings")
                qmp.capture(artifacts / "shade-expanded-dark.ppm")
                qmp.click(350, 200)
                time.sleep(.5)
                qmp.drag(280, 394, 170, 394, artifacts / "shade-volume-drag.ppm")
                guest.wait_setting("volume", 25)
                qmp.click(120, 292)  # Mute
                time.sleep(.3)
                guest.wait_setting("volume", 0)
                qmp.click(120, 292)  # Restore the previous volume
                time.sleep(.3)
                guest.wait_setting("volume", 25)
                qmp.click(432, 824)  # Power menu
                time.sleep(.3)
                qmp.capture(artifacts / "shade-power.ppm")
                qmp.click(240, 586)  # Cancel
                time.sleep(.3)
                qmp.drag(240, 880, 240, 100)  # Close and restore the app
                restored = qmp.capture(artifacts / "shade-restored-app.ppm")
                def result_region(frame):
                    return b"".join(frame[(y * 480 + 320) * 3:(y * 480 + 444) * 3]
                                    for y in range(160, 220))
                if result_region(calculator) != result_region(restored):
                    raise RuntimeError("Closing the shade did not preserve calculator contents")
                processes = json.loads(guest.run("lmpkg running"))
                if processes.get("org.leon.calculator") != calculator_pid:
                    raise RuntimeError("Shade changed the underlying application process")
                qmp.click(72, 384)  # Input reaches the restored calculator
                time.sleep(.3)
                if result_region(qmp.capture(artifacts / "shade-restored-input.ppm")) == result_region(restored):
                    raise RuntimeError("Shade closure did not restore application pointer input")
                qmp.click(240, 940)
                time.sleep(.5)
                qmp.click(400, 22)
                time.sleep(.5)
                qmp.click(372, 628)  # Settings shortcut in compact shade
                # Mapping a new process and populating its first font cache can
                # take several seconds under software CPU emulation.
                for attempt in range(30):
                    frame = qmp.capture(artifacts / "settings-light.ppm")
                    point = (196 * 480 + 80) * 3
                    row = (380 * 480 + 430) * 3
                    if (frame[point:point + 3] == bytes.fromhex("eaddff") and
                            frame[row:row + 3] == bytes.fromhex("fffbff")):
                        break
                    time.sleep(.25)
                else:
                    raise RuntimeError("Settings did not render its interactive controls")
                # Settings now has a category home, separate detail pages and
                # a real search field. Exercise the same public UI as a user.
                qmp.click(180, 380)  # Display
                time.sleep(.5)
                qmp.capture(artifacts / "settings-display-light.ppm")
                qmp.click(400, 257)  # Dark theme switch
                time.sleep(1)
                qmp.capture(artifacts / "settings-display-dark.ppm")
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("dark") is not True:
                    raise RuntimeError("Settings UI did not persist the dark theme")
                qmp.click(36, 75)  # Back to categories
                time.sleep(.4)
                qmp.capture(artifacts / "settings-dark.ppm")
                qmp.click(180, 206)  # Search settings
                for code in "volume":
                    qmp.key(code)
                time.sleep(.5)
                qmp.capture(artifacts / "settings-search.ppm")
                qmp.click(180, 296)  # Matching Sound result
                time.sleep(.5)
                qmp.click(146, 346)  # 25% of the playback slider
                time.sleep(.5)
                qmp.capture(artifacts / "settings-sound.ppm")
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("volume") != 25:
                    raise RuntimeError("Settings UI did not apply the playback volume")
                qmp.click(36, 75)
                time.sleep(.3)
                qmp.click(436, 206)  # Clear search, restore all categories
                time.sleep(.3)
                qmp.click(180, 296)  # Network & internet
                time.sleep(.5)
                qmp.capture(artifacts / "settings-network.ppm")
                qmp.click(36, 75)
                time.sleep(.3)
                qmp.click(180, 548)  # Storage
                time.sleep(.5)
                qmp.capture(artifacts / "settings-storage.ppm")
                qmp.click(36, 75)
                time.sleep(.3)
                qmp.click(180, 632)  # System
                time.sleep(.3)
                qmp.capture(artifacts / "settings-system.ppm")
                qmp.click(180, 244)  # Languages
                time.sleep(.3)
                qmp.click(180, 328)  # English
                time.sleep(.5)
                qmp.capture(artifacts / "settings-english.ppm")
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("language") != "en":
                    raise RuntimeError("Language selection did not persist")
                qmp.click(180, 244)  # Restore simplified Chinese
                time.sleep(.3)
                qmp.click(36, 75)
                time.sleep(.3)
                qmp.click(180, 328)  # System navigation
                time.sleep(.3)
                qmp.click(350, 320)  # 3-button navigation
                time.sleep(.5)
                qmp.capture(artifacts / "settings-navigation-buttons.ppm")
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("navigation") != "buttons":
                    raise RuntimeError("Navigation selection did not persist")
                qmp.click(120, 320)  # Restore gestures
                time.sleep(.3)
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("navigation") != "gestures" or preferences.get("language") != "zh-CN":
                    raise RuntimeError("Settings UI could not restore language/navigation")
                qmp.click(36, 75)
                time.sleep(.3)
                qmp.click(36, 75)
                time.sleep(.3)
                qmp.click(180, 716)  # About device
                time.sleep(.5)
                qmp.capture(artifacts / "settings-about.ppm")
                network = guest.run("ip -4 addr show dev eth0; ip route show default; cat /etc/resolv.conf")
                if "10.0.2.15" not in network or "default via 10.0.2.2" not in network or "nameserver 10.0.2.3" not in network:
                    raise RuntimeError("QEMU DHCP, routing or DNS configuration failed")
                guest.run("ping -c 1 -W 3 10.0.2.2")
                (artifacts / "network.txt").write_text(network + "\n")
                audio = guest.run("XDG_RUNTIME_DIR=/run/user/1000 wpctl status")
                volume = guest.run("XDG_RUNTIME_DIR=/run/user/1000 wpctl get-volume @DEFAULT_AUDIO_SINK@")
                if "Volume: 0.25" not in volume:
                    raise RuntimeError("PipeWire sink volume does not match the UI setting")
                (artifacts / "audio.txt").write_text(audio + "\n" + volume + "\n")
                # Refactored built-ins must launch the ELF named in their
                # own manifest and retain their established Unix identity.
                for app in apps:
                    app_id = app["id"]
                    guest.run("lmpkg launch " + shlex.quote(app_id))
                    time.sleep(.3)
                    processes = json.loads(guest.run("lmpkg running"))
                    if app_id not in processes:
                        raise RuntimeError("Built-in exited after launch: " + app_id)
                    pid = processes[app_id]
                    # The non-root console cannot dereference another UID's
                    # /proc/PID/exe; comm and status are public process data.
                    actual = guest.run(f"cat /proc/{pid}/comm")
                    if actual != Path(app["entry"]).name[:15]:
                        raise RuntimeError(f"{app_id} launched unexpected command {actual}")
                    identity = guest.run(f"cat /proc/{pid}/status")
                    uid = re.search(r"^Uid:\s+(\d+)", identity, re.M)
                    if not uid or int(uid[1]) != app["uid"]:
                        raise RuntimeError("Built-in identity changed: " + app_id)
                if args.package:
                    path = "/data/home/leon/hello.lmpkg"
                    guest.upload(args.package, path)
                    guest.run(f"lmpkg install {path}")
                    guest.run("lmpkg launch org.example.hello")
                    time.sleep(1)
                    if "org.example.hello" not in json.loads(guest.run("lmpkg running")):
                        raise RuntimeError("Installed SDK application did not remain running")
                    qmp.capture(artifacts / "sdk-hello.ppm")
                    guest.run("lmpkg remove org.example.hello --purge-data")
                    if any(app["id"] == "org.example.hello" for app in json.loads(guest.run("lmpkg list"))):
                        raise RuntimeError("SDK package removal failed")
                qmp.click(400, 22)
                time.sleep(.5)
                qmp.click(400, 22)  # Expand the shade
                time.sleep(.5)
                qmp.click(350, 292)  # Lock screen tile
                time.sleep(.5)
                locked = qmp.capture(artifacts / "shade-locked.ppm")
                qmp.click(400, 22)  # Locked session cannot expose the shade
                time.sleep(.4)
                still_locked = qmp.capture(artifacts / "shade-locked-top.ppm")
                def lock_header(frame):
                    return b"".join(frame[(y * 480 + 24) * 3:(y * 480 + 200) * 3]
                                    for y in range(60, 150))
                if lock_header(locked) != lock_header(still_locked):
                    raise RuntimeError("Locked session exposed the notification shade")
                qmp.drag(240, 20, 240, 300)
                if lock_header(qmp.capture(artifacts / "shade-locked-pull.ppm")) != lock_header(locked):
                    raise RuntimeError("Pulling the locked status bar changed the session")
                qmp.click(240, 790)
                time.sleep(.5)
                unlocked = qmp.capture(artifacts / "shade-unlocked-home.ppm")
                qmp.click(400, 22)
                time.sleep(.5)
                qmp.click(8, 810)  # Dismiss through the backdrop
                qmp.wait_frame(artifacts / "shade-backdrop-dismissed.ppm",
                               lambda frame: lock_header(frame) == lock_header(unlocked),
                               "Shade backdrop did not return to the home screen")
                qmp.click(400, 22)
                time.sleep(.5)
                qmp.click(432, 628)
                time.sleep(.3)
                qmp.click(240, 426)  # Explicit power-off choice
                try:
                    proc.wait(timeout=20)
                except subprocess.TimeoutExpired as error:
                    raise RuntimeError("UI poweroff did not stop QEMU") from error
                if proc.returncode:
                    raise RuntimeError("QEMU exited with status " + str(proc.returncode))
                if "leond: exited compositor status=0 retry=0" not in log.read_text(errors="replace"):
                    raise RuntimeError("Compositor did not shut down cleanly")
                passed = True
                print("QEMU boot, native apps, shade drag/controls/focus, settings and clean UI poweroff passed")
                print("Artifacts:", artifacts)
            finally:
                if proc.poll() is None:
                    try:
                        proc.stdin.write(b"\x03sync\n")
                        proc.stdin.flush()
                        time.sleep(.5)
                    except BrokenPipeError:
                        pass
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
                if qmp:
                    qmp.stream.close()
                    qmp.socket.close()
                if not passed:
                    subprocess.run(["cp", "--sparse=always", disk,
                                    artifacts / "failed-disk.img"], check=True)


if __name__ == "__main__":
    main()
