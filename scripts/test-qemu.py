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

    def click(self, x, y):
        self.command("input-send-event", {"events": [
            {"type": "abs", "data": {"axis": "x", "value": int(x/480*32767)}},
            {"type": "abs", "data": {"axis": "y", "value": int(y/960*32767)}}]})
        time.sleep(.05)
        self.command("input-send-event", {"events": [
            {"type": "btn", "data": {"down": True, "button": "left"}}]})
        time.sleep(.1)
        self.command("input-send-event", {"events": [
            {"type": "btn", "data": {"down": False, "button": "left"}}]})

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=int, default=150)
    parser.add_argument("--package", type=Path, help="also install, launch and remove the SDK hello package")
    args = parser.parse_args()
    if args.package and not args.package.is_file():
        parser.error("--package must name a compiled SDK hello package")
    images = REPO / "output/images"
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
                while time.monotonic() < deadline:
                    try:
                        qmp.capture(artifacts / "lock.ppm")
                        break
                    except RuntimeError:
                        time.sleep(.5)
                else:
                    raise RuntimeError("Desktop did not render; inspect guest compositor logs")
                qmp.click(240, 790)
                time.sleep(1)
                qmp.capture(artifacts / "home.ppm")
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
                qmp.capture(artifacts / "calculator-result.ppm")
                qmp.click(240, 940)
                time.sleep(.5)
                qmp.click(240, 838)
                time.sleep(.5)
                qmp.click(96, 426)
                # Mapping a new process and populating its first font cache can
                # take several seconds under software CPU emulation.
                for attempt in range(30):
                    frame = qmp.capture(artifacts / "settings-light.ppm")
                    point = (242 * 480 + 440) * 3
                    if frame[point:point + 3] == bytes.fromhex("d5cadd"):
                        break
                    time.sleep(.25)
                else:
                    raise RuntimeError("Settings did not render its interactive controls")
                qmp.click(400, 242)
                time.sleep(1)
                qmp.capture(artifacts / "settings-dark.ppm")
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("dark") is not True:
                    raise RuntimeError("Settings UI did not persist the dark theme")
                qmp.click(133, 480)
                time.sleep(.5)
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("volume") != 25:
                    raise RuntimeError("Settings UI did not apply the playback volume")
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
                qmp.click(240, 526)
                try:
                    proc.wait(timeout=20)
                except subprocess.TimeoutExpired as error:
                    raise RuntimeError("UI poweroff did not stop QEMU") from error
                if proc.returncode:
                    raise RuntimeError("QEMU exited with status " + str(proc.returncode))
                if "leond: exited compositor status=0 retry=0" not in log.read_text(errors="replace"):
                    raise RuntimeError("Compositor did not shut down cleanly")
                passed = True
                print("QEMU boot, native app listing, display input and clean UI poweroff passed")
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
