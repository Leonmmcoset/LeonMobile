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
import shutil
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

    def wait_lifecycle(self, foreground, pids=None, absent=(), events=None):
        pids, events = pids or {}, events or {}
        expected = {foreground} if foreground else set()
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            response = json.loads(self.run("lmpkg lifecycle"))
            states = response["applications"]
            active = {app for app, state in states.items() if state["foreground"]}
            if (active == expected and all(app not in states for app in absent) and
                    all(app in states and states[app]["pid"] == pid and
                        not states[app]["stopping"] and not states[app]["pending"] and
                        states[app]["responsive"] for app, pid in pids.items()) and
                    all(app in states and states[app]["event"] == event
                        for app, event in events.items())):
                return response
            time.sleep(.1)
        raise RuntimeError("Application lifecycle did not settle: " + json.dumps(response))


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

    def chord(self, modifiers, code):
        def event(key, down):
            self.command("input-send-event", {"events": [{"type": "key", "data": {
                "down": down, "key": {"type": "qcode", "data": key}}}]})
        for modifier in modifiers:
            event(modifier, True)
        self.key(code)
        for modifier in reversed(modifiers):
            event(modifier, False)

    def type_text(self, text):
        plain = dict(zip("`-=[]\\;',./ ", ("grave_accent", "minus", "equal", "bracket_left",
                         "bracket_right", "backslash", "semicolon", "apostrophe", "comma", "dot", "slash", "spc")))
        shifted = dict(zip('~!@#$%^&*()_+{}|:"<>?', ("grave_accent", "1", "2", "3", "4", "5", "6",
                            "7", "8", "9", "0", "minus", "equal", "bracket_left", "bracket_right",
                            "backslash", "semicolon", "apostrophe", "comma", "dot", "slash")))
        for character in text:
            code = plain.get(character, character.lower())
            modifiers = []
            if character in shifted:
                code, modifiers = shifted[character], ["shift"]
            elif character.isupper():
                modifiers = ["shift"]
            self.chord(modifiers, code)

    def capture(self, path, allow_blank=False):
        self.command("screendump", {"filename": str(path)})
        with path.open("rb") as stream:
            if stream.readline().strip() != b"P6":
                raise RuntimeError("QEMU did not produce a PPM display frame")
            size = stream.readline().strip()
            maximum = stream.readline().strip()
            pixels = stream.read()
        if size != b"480 960" or maximum != b"255" or len(pixels) != 480 * 960 * 3:
            raise RuntimeError("Expected a complete 480x960 portrait display")
        if not allow_blank and len({pixels[i:i + 3] for i in range(0, len(pixels), 48)}) < 16:
            raise RuntimeError("QEMU display is blank")
        return pixels

    def wait_frame(self, path, predicate, description):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            # New windows and transparent page transitions may expose a blank
            # frame before the client commits. The expected geometry below,
            # rather than a premature screenshot error, defines readiness.
            frame = self.capture(path, allow_blank=True)
            if predicate(frame):
                return frame
            time.sleep(.15)
        raise RuntimeError(description)

    def wait_shade(self, path, bottom, dark=False):
        background = bytes.fromhex("15121b" if dark else "fff8ff")
        outline = bytes.fromhex("4a4258" if dark else "d5cadd")
        def pixel(frame, x, y):
            offset = (y * 480 + x) * 3
            return frame[offset:offset + 3]
        return self.wait_frame(
            path, lambda frame: (pixel(frame, 240, bottom - 17) == background and
                                 pixel(frame, 240, bottom - 15) == outline),
            "Notification shade did not settle to " + str(bottom))


def inject_probes(images, disk, temporary, notification_probe, system_probe, terminal_probe=None):
    """Inject test-only helpers into the cloned disk; release images stay intact."""
    if not notification_probe and not system_probe and not terminal_probe:
        return
    rootfs = temporary / "test-root.ext4"
    subprocess.run(["cp", "--reflink=auto", "--sparse=always", images / "rootfs.ext4", rootfs], check=True)
    commands = []
    def put(source, destination):
        # debugfs has its own command parser; no shell interprets these paths.
        commands.append("write " + json.dumps(str(source.resolve())) + " " + destination)
        commands.append("set_inode_field " + destination + " mode 0100755")
    if notification_probe:
        put(notification_probe, "/usr/bin/leon-test-notifications")
    if terminal_probe:
        put(terminal_probe, "/usr/bin/leon-test-terminal")
    if system_probe:
        put(system_probe, "/usr/bin/leon-test-system")
        unit = temporary / "test-service.toml"
        unit.write_text('name = "system-test"\ncommand = ["/usr/bin/leon-test-system"]\n'
                        'after = ["apps", "memory", "power", "storage", "network", "time", "crash", "portal", "input-method"]\n'
                        'type = "oneshot"\nrestart = "never"\n')
        put(unit, "/etc/leon/services/99-system-test.toml")
    batch = temporary / "debugfs.commands"
    batch.write_text("\n".join(commands) + "\n")
    result = subprocess.run(["debugfs", "-w", "-f", batch, rootfs], capture_output=True, text=True, check=True)
    if "File not found" in result.stderr or "not found" in result.stderr or "already exists" in result.stderr:
        raise RuntimeError("Could not inject target test helpers: " + result.stderr)
    table = json.loads(subprocess.check_output(["sfdisk", "--json", disk]))["partitiontable"]
    partition = table["partitions"][0]
    sector_size = table.get("sectorsize", 512)
    if rootfs.stat().st_size > partition["size"] * sector_size:
        raise RuntimeError("Test filesystem is larger than the system partition")
    with disk.open("r+b") as destination, rootfs.open("rb") as source:
        destination.seek(partition["start"] * sector_size)
        shutil.copyfileobj(source, destination)


def exercise_core_ui(guest,qmp,artifacts,preferences):
    # Exercise the real text-input-v3 -> input-method-v2 -> LMUI
    # path with pointer events, then inspect the saved UTF-8 file.
    qmp.click(240, 940)
    primary=bytes.fromhex("d0bcff" if preferences.get("dark") else "6750a4")
    qmp.wait_frame(artifacts / "ime-home-ready.ppm",
                   lambda frame: frame[(210*480+240)*3:(210*480+240)*3+3]==primary,
                   "Home did not finish its entrance before opening the editor")
    time.sleep(.2)
    qmp.click(384, 410)  # Editor on home
    save_color=bytes.fromhex("d0bcff" if preferences.get("dark") else "6750a4")
    qmp.wait_frame(artifacts / "editor-ready.ppm",
                   lambda frame: frame[(844*480+24)*3:(844*480+24)*3+3]==save_color,
                   "Editor did not reach full-height controls")
    time.sleep(.5)
    qmp.click(180, 250)  # Text field
    expected_keyboard=bytes.fromhex("30293c" if preferences.get("dark") else "eaddff")
    qmp.wait_frame(artifacts / "ime-open.ppm",
                   lambda frame: frame[(604*480+3)*3:(604*480+3)*3+3]==expected_keyboard,
                   "Shared input method did not appear")
    time.sleep(.5)
    positions={'n':(346,805),'i':(356,703),'h':(292,754),'a':(34,754),'o':(402,703)}
    for code in "nihao":
        qmp.click(*positions[code]);time.sleep(.25)
    qmp.capture(artifacts / "ime-pinyin-candidates.ppm")
    qmp.click(35,652)  # 你好
    time.sleep(.3)
    qmp.click(357,864)  # UTF-8 backspace removes 好
    time.sleep(.3)
    for code in "hao":
        qmp.click(*positions[code]);time.sleep(.25)
    qmp.click(35,652)
    time.sleep(.4)
    qmp.capture(artifacts / "ime-committed-chinese.ppm")
    # The trusted desktop owns imed's UID; killing it is an
    # actual service failure, followed by leond recovery.
    old=guest.run("pidof imed")
    guest.run("kill -KILL " + old)
    limit=time.monotonic()+12
    while time.monotonic()<limit:
        current=guest.run("pidof imed || true")
        if current and current!=old:break
        time.sleep(.2)
    else:raise RuntimeError("Input method did not restart")
    time.sleep(.8)
    qmp.wait_frame(artifacts / "ime-restarted.ppm",
                   lambda frame: frame[(604*480+3)*3:(604*480+3)*3+3]==expected_keyboard,
                   "Restarted IME did not restore the active text field")
    qmp.key("esc")
    save_color=bytes.fromhex("d0bcff" if preferences.get("dark") else "6750a4")
    qmp.wait_frame(artifacts / "ime-hidden.ppm",
                   lambda frame: frame[(844*480+24)*3:(844*480+24)*3+3]==save_color,
                   "Editor did not restore full-height controls after hiding IME")
    time.sleep(.25)
    qmp.click(180,844)  # Save the Chinese note
    time.sleep(.6)
    # A separate broker request must dispatch save without moving the app to
    # the background or changing its PID, then observe the app's own ack.
    saved=json.loads(guest.run("lmpkg save org.leon.editor"))["applications"]["org.leon.editor"]
    limit=time.monotonic()+5
    while time.monotonic()<limit:
        state=json.loads(guest.run("lmpkg lifecycle"))["applications"]["org.leon.editor"]
        if state["event"]=="save" and not state["pending"]:break
        time.sleep(.1)
    else:raise RuntimeError("Application did not acknowledge the explicit save request")
    if not state["responsive"] or not state["foreground"] or state["pid"]!=saved["pid"]:
        raise RuntimeError("Explicit checkpoint failed or changed the foreground application")

    def wait_permission(file_request):
        limit=time.monotonic()+8
        while time.monotonic()<limit:
            pending=json.loads(guest.run("lmsys portal '{\"op\":\"pending\"}'"))["requests"]
            if pending and pending[0].get("file_request",False)==file_request:
                # The editor's document and the modal share the surface color.
                # Observe a modal-specific button instead of that background.
                expected=bytes.fromhex(
                    ("30293c" if preferences.get("dark") else "eaddff") if file_request
                    else ("d0bcff" if preferences.get("dark") else "6750a4"))
                px,py=(180,376) if file_request else (60,542)
                qmp.wait_frame(artifacts / ("picker-ready.ppm" if file_request else "permission-ready.ppm"),
                               lambda frame: frame[(py*480+px)*3:(py*480+px)*3+3]==expected,
                               "Permission modal did not render")
                time.sleep(.2);return pending[0]
            time.sleep(.2)
        raise RuntimeError("Permission dialog did not reach the expected stage")
    qmp.click(410,844)  # Import
    wait_permission(False)
    qmp.capture(artifacts / "permission-request.ppm")
    qmp.click(240,602)  # Deny
    qmp.wait_frame(artifacts / "permission-denied.ppm",
                   lambda frame: frame[(844*480+24)*3:(844*480+24)*3+3]==save_color,
                   "Permission denial did not restore editor")
    time.sleep(.3)
    qmp.click(410,844)  # Retry
    wait_permission(False)
    qmp.click(240,542)  # Allow
    wait_permission(True)
    qmp.capture(artifacts / "permission-file-picker.ppm")
    qmp.click(180,376)  # target-note.txt, selected by the user
    limit=time.monotonic()+8
    while time.monotonic()<limit:
        pending=json.loads(guest.run("lmsys portal '{\"op\":\"pending\"}'"))["requests"]
        if not pending:break
        time.sleep(.2)
    else:raise RuntimeError("Selected document did not complete")
    time.sleep(.7)
    qmp.capture(artifacts / "permission-imported-editor.ppm")
    qmp.key("esc")
    time.sleep(.2)
    qmp.click(436,84)  # Close editor; saves lifecycle state
    time.sleep(.5)
    print("Target IME/portal: Chinese candidates, UTF-8 delete, service restart, explicit checkpoint, "
          "permission denial/approval and file selection exercised",flush=True)

def exercise_terminal_ui(guest, qmp, artifacts):
    # This path uses real compositor input and the terminal's sandboxed shell.
    # Files stay private and are inspected from ext4 after clean shutdown.
    def command(text):
        qmp.type_text(text)
        qmp.key("ret")
        time.sleep(.3)
    command("/usr/bin/leon-test-terminal > terminal-tests.txt")
    time.sleep(2)
    command("printf '\\033[2J\\033[H\\033[48;2;12;34;56m    \\033[0mANSI_READY\\n'")
    qmp.wait_frame(artifacts / "terminal-truecolor.ppm",
                   lambda frame: frame[(142 * 480 + 28) * 3:(142 * 480 + 28) * 3 + 3]
                                 == bytes((12, 34, 56)),
                   "Terminal did not render an ANSI RGB background on blank cells")
    command("sleep 60")
    qmp.chord(["ctrl"], "c")
    command("printf interrupted > terminal-interrupt.txt")
    command("vi terminal-edited.txt")
    time.sleep(.8)
    qmp.type_text("iLeonMobile terminal")
    qmp.key("esc")
    command(":wq")
    time.sleep(.8)
    command("top")
    time.sleep(1)
    qmp.capture(artifacts / "terminal-top.ppm")
    qmp.key("q")
    time.sleep(.5)
    command("printf '\\033[2J\\033[HCLIP_TEXT\\n'")
    time.sleep(.5)
    # Select an entire grid line; trailing blanks are removed by copy.
    qmp.click(54, 840)  # Select (app origin 42, height 854)
    qmp.drag(27, 149, 450, 149)
    qmp.click(130, 840)  # Copy
    command("read value; printf '%s' \"$value\" > terminal-pasted.txt")
    qmp.chord(["ctrl", "shift"], "v")
    time.sleep(.4)
    qmp.key("ret")
    time.sleep(.3)
    qmp.capture(artifacts / "terminal-clipboard.ppm")
    command("exit 7")
    time.sleep(.5)
    qmp.click(424, 840)  # Restart session
    time.sleep(.5)
    command("printf restarted > terminal-restarted.txt")
    qmp.capture(artifacts / "terminal-restarted.ppm")
    print("Target terminal: PTY/VT probe, RGB blank cells, Ctrl-C, vi, top, clipboard and restart exercised", flush=True)

def verify_terminal_data(disk, temp):
    table = json.loads(subprocess.check_output(["sfdisk", "--json", disk]))["partitiontable"]
    partition = table["partitions"][1]
    datafs = temp / "terminal-data.ext4"
    with disk.open("rb") as source, datafs.open("wb") as destination:
        source.seek(partition["start"] * table.get("sectorsize", 512))
        remaining = partition["size"] * table.get("sectorsize", 512)
        while remaining:
            block = source.read(min(1024 * 1024, remaining))
            if not block:
                raise RuntimeError("Short terminal data partition")
            destination.write(block)
            remaining -= len(block)
    def read(name):
        return subprocess.check_output(["debugfs", "-R", "cat /leon/data/org.leon.terminal/" + name,
                                        datafs], stderr=subprocess.DEVNULL).decode()
    report = read("terminal-tests.txt")
    if "PASS terminal ANSI/VT" not in report or "PASS terminal real PTY" not in report:
        raise RuntimeError("Target terminal probe did not pass: " + report)
    for name, expected in (("terminal-interrupt.txt", "interrupted"),
                            ("terminal-edited.txt", "LeonMobile terminal\n"),
                            ("terminal-pasted.txt", "CLIP_TEXT"), ("terminal-restarted.txt", "restarted")):
        actual = read(name)
        if actual != expected:
            raise RuntimeError(name + " contains " + repr(actual) + ", expected " + repr(expected))
    print("Offline terminal data: VT/PTY probe, interrupt, vi edit, clipboard and restarted shell verified", flush=True)

def verify_core_data(disk,temp):
    table=json.loads(subprocess.check_output(["sfdisk","--json",disk]))["partitiontable"]
    data_partition=table["partitions"][1]
    datafs=temp/"verified-data.ext4"
    with disk.open("rb") as source,datafs.open("wb") as destination:
        source.seek(data_partition["start"]*table.get("sectorsize",512))
        remaining=data_partition["size"]*table.get("sectorsize",512)
        while remaining:
            block=source.read(min(1024*1024,remaining))
            if not block:raise RuntimeError("Short data partition")
            destination.write(block);remaining-=len(block)
    def private_file(path):
        return subprocess.check_output(["debugfs","-R","cat "+path,datafs],stderr=subprocess.DEVNULL).decode()
    note=private_file("/leon/data/org.leon.editor/note.txt")
    if note!="你好":raise RuntimeError("Wayland IME saved unexpected UTF-8 note: "+repr(note))
    session=json.loads(private_file("/leon/data/org.leon.editor/.session.json"))["state"]
    if "/imports/document-" not in session["path"] or session["text"]!="LeonMobile 中文\n":
        raise RuntimeError("Selected file did not reach the editor's private saved state")
    print("Offline private data: exact Chinese note and imported editor state verified",flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=int, default=150)
    parser.add_argument("--images", type=Path, default=REPO / "output/images",
                        help="image directory, including an assembled local release")
    parser.add_argument("--package", type=Path, help="also install, launch and remove the SDK hello package")
    parser.add_argument("--notification-probe", type=Path,
                        help="AArch64 tests/target/notifications.c executable for live notification checks")
    parser.add_argument("--system-probe", type=Path,
                        help="AArch64 system service/sandbox probe, injected only into a private disk")
    parser.add_argument("--terminal-probe", type=Path,
                        help="AArch64 terminal VT/PTY probe plus interactive shell/vi/top/clipboard tests")
    parser.add_argument("--core-only",action="store_true",
                        help="run service fault probes and focused IME/permission UI validation")
    args = parser.parse_args()
    if args.core_only and not args.system_probe:parser.error("--core-only requires --system-probe")
    if args.package and not args.package.is_file():
        parser.error("--package must name a compiled SDK hello package")
    if args.notification_probe and not args.notification_probe.is_file():
        parser.error("--notification-probe must name a compiled AArch64 probe")
    if args.system_probe and not args.system_probe.is_file():
        parser.error("--system-probe must name a compiled AArch64 probe")
    if args.terminal_probe and not args.terminal_probe.is_file():
        parser.error("--terminal-probe must name a compiled AArch64 probe")
    if args.terminal_probe and args.core_only:
        parser.error("--terminal-probe requires the full interactive sequence")
    images = args.images.resolve()
    artifacts = REPO / "build/qemu-test"
    artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="leon-qemu-") as temp:
        temp = Path(temp)
        disk = temp / "disk.img"
        subprocess.run(["cp", "--reflink=auto", "--sparse=always",
                        images / "leonmobile-qemu-aarch64.img", disk], check=True)
        inject_probes(images, disk, temp, args.notification_probe, args.system_probe, args.terminal_probe)
        media = temp / "removable.ext4"
        if args.system_probe:
            with media.open("wb") as stream: stream.truncate(16 * 1024 * 1024)
            subprocess.run(["mkfs.ext4", "-q", "-F", media], check=True)
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
        if args.system_probe:
            command += ["-drive", f"if=none,file={media},format=raw,id=removable",
                        "-device", "usb-storage,drive=removable,removable=on"]
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
                if args.system_probe:
                    limit = time.monotonic() + 150
                    result = ""
                    while time.monotonic() < limit:
                        result = guest.run("cat /run/leon/system-tests.txt 2>/dev/null || true")
                        if "SYSTEM_TESTS_FAILED" in result:
                            raise RuntimeError("Target system tests failed:\n" + result)
                        if "SYSTEM_TESTS_PASSED" in result: break
                        time.sleep(.3)
                    else: raise RuntimeError("Target system tests timed out:\n" + result)
                    (artifacts / "system-services.txt").write_text(result + "\n")
                    print(result, flush=True)
                services = guest.run("leonctl status")
                (artifacts / "notifications-service.txt").write_text(services + "\n")
                service_pids = {}
                for service in ("apps", "notifications", "memory", "power", "storage", "network", "time", "crash", "portal", "input-method"):
                    running = re.search(rf"^{service}\trunning\tpid=(\d+)", services, re.M)
                    if not running:
                        raise RuntimeError("Required service is not running: " + service)
                    service_pids[service] = int(running[1])
                if service_pids["apps"] == service_pids["notifications"]:
                    raise RuntimeError("Notifications must run in an independent daemon")
                if guest.run(f"cat /proc/{service_pids['notifications']}/comm") != "notificationd":
                    raise RuntimeError("Notification service launched an unexpected executable")
                guest.run("test -x /usr/sbin/notificationd && test -S /run/leon/notifications.sock")
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
                # A short lock-screen drag follows the contact, then returns
                # without unlocking. Compare the static instruction region.
                lock = frame
                qmp.move(240, 790)
                time.sleep(.05)
                qmp.button(True)
                qmp.move(240, 760)
                time.sleep(.2)
                held = qmp.capture(artifacts / "lock-dragging.ppm")
                qmp.button(False)
                time.sleep(.6)
                settled = qmp.capture(artifacts / "lock-settled.ppm")
                def lock_hint(pixels):
                    return pixels[730 * 480 * 3:850 * 480 * 3]
                if lock_hint(held) == lock_hint(lock) or lock_hint(settled) != lock_hint(lock):
                    raise RuntimeError("Short lock drag did not track and settle without unlocking")
                qmp.click(240, 790)
                time.sleep(1)
                qmp.capture(artifacts / "home.ppm")
                if args.core_only:
                    preferences=json.loads(guest.run("cat /data/leon/settings.json"))
                    exercise_core_ui(guest,qmp,artifacts,preferences)
                    guest.run("lmsys power '{\"op\":\"poweroff\"}'")
                    proc.wait(timeout=25)
                    if proc.returncode:raise RuntimeError("QEMU core test shutdown failed")
                    if "leond: exited compositor status=0 retry=0" not in log.read_text(errors="replace"):
                        raise RuntimeError("Compositor did not stop cleanly after core tests")
                    verify_core_data(disk,temp)
                    passed=True
                    print("Core service, isolation, IME and permission UI target validation passed",flush=True)
                    return
                notification_terminal = None
                if args.notification_probe or args.terminal_probe:
                    qmp.click(240, 838)  # All apps
                    time.sleep(.5)
                    qmp.click(240, 426)  # Terminal
                    qmp.wait_frame(
                        artifacts / "shade-notification-terminal.ppm",
                        lambda frame: frame[(180 * 480 + 24) * 3:(180 * 480 + 24) * 3 + 3]
                                      == bytes.fromhex("19151f"),
                        "Terminal did not render for notification probe")
                    if args.terminal_probe:
                        guest.run("test -x /usr/bin/leon-test-terminal && test -r /usr/share/terminfo/x/xterm-256color")
                        exercise_terminal_ui(guest, qmp, artifacts)
                if args.notification_probe:
                    guest.run("test -x /usr/bin/leon-test-notifications")
                    for code in "/usr/bin/leon-test-notifications":
                        qmp.key({"/": "slash", "-": "minus"}.get(code, code))
                    qmp.key("ret")
                    expected = [{"app": "org.leon.terminal",
                                 "title": f"LeonMobile 测试通知 {index:02d}"}
                                for index in range(1, 13)]
                    deadline = time.monotonic() + 15
                    while True:
                        notifications = json.loads(guest.run("/usr/bin/leon-test-notifications --list"))["notifications"]
                        if notifications == expected or time.monotonic() >= deadline:
                            break
                        time.sleep(.2)
                    legacy = json.loads(guest.run("/usr/bin/leon-test-notifications --legacy-list"))["notifications"]
                    if notifications != expected or legacy != notifications:
                        raise RuntimeError("Direct/legacy notifications lost their peer identity or shared queue")
                    (artifacts / "notifications.json").write_text(
                        json.dumps(notifications, ensure_ascii=False, indent=2) + "\n")
                    notification_terminal = qmp.capture(artifacts / "shade-notification-terminal.ppm")
                if args.notification_probe or args.terminal_probe:
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
                calculator_id = "org.leon.calculator"
                retained = {calculator_id: calculator_pid}
                guest.wait_lifecycle(calculator_id, retained)
                qmp.click(240, 940)  # Home backgrounds without killing the app
                guest.wait_lifecycle(None, retained, events={calculator_id: "background"})
                qmp.wait_frame(
                    artifacts / "lifecycle-home.ppm",
                    lambda frame: frame[(210 * 480 + 240) * 3:(210 * 480 + 240) * 3 + 3]
                                  == bytes.fromhex("6750a4"),
                    "Home did not settle before resuming the calculator")
                qmp.click(96, 410)
                guest.wait_lifecycle(calculator_id, retained,
                                     events={calculator_id: "foreground"})
                qmp.wait_frame(
                    artifacts / "lifecycle-home-resumed.ppm",
                    lambda frame: frame[160 * 480 * 3:220 * 480 * 3]
                                  == calculator[160 * 480 * 3:220 * 480 * 3],
                    "Home activation did not preserve the calculator state")
                qmp.click(400, 940)  # All apps uses the same background path
                guest.wait_lifecycle(None, retained, events={calculator_id: "background"})
                # Returning through Recents must also update broker priority;
                # otherwise memory pressure could reclaim the visible app.
                qmp.click(80, 940)
                qmp.wait_frame(
                    artifacts / "lifecycle-recent-tasks.ppm",
                    lambda frame: (frame[(180 * 480 + 30) * 3:(180 * 480 + 30) * 3 + 3]
                                   == bytes.fromhex("eaddff") and
                                   frame[(140 * 480 + 240) * 3:(140 * 480 + 240) * 3 + 3]
                                   == bytes.fromhex("fff8ff")),
                    "Recent tasks did not become interactive")
                guest.wait_lifecycle(None, retained)
                qmp.click(120, 190)
                qmp.wait_frame(
                    artifacts / "lifecycle-resumed-calculator.ppm",
                    lambda frame: frame[160 * 480 * 3:220 * 480 * 3]
                                  == calculator[160 * 480 * 3:220 * 480 * 3],
                    "Recent tasks did not restore the calculator")
                guest.wait_lifecycle(calculator_id, retained,
                                     events={calculator_id: "foreground"})
                if guest.run(f"cat /proc/{calculator_pid}/oom_score_adj") != "0":
                    raise RuntimeError("Recent tasks did not restore foreground OOM priority")
                # Dismiss through the real task UI, then verify checkpoint
                # restoration in a new process rather than a cached window.
                qmp.click(80, 940)
                guest.wait_lifecycle(None, retained, events={calculator_id: "background"})
                qmp.wait_frame(
                    artifacts / "lifecycle-before-close.ppm",
                    lambda frame: (frame[(180 * 480 + 30) * 3:(180 * 480 + 30) * 3 + 3]
                                   == bytes.fromhex("eaddff") and
                                   frame[(140 * 480 + 240) * 3:(140 * 480 + 240) * 3 + 3]
                                   == bytes.fromhex("fff8ff")),
                    "Recent tasks did not settle before cooperative close")
                qmp.click(432, 190)
                closed = guest.wait_lifecycle(None, absent=(calculator_id,))
                if [record for record in closed["exits"] if record["pid"] == calculator_pid] != [
                        {"app": calculator_id, "pid": calculator_pid, "status": 0}]:
                    raise RuntimeError("Task checkpoint did not end in one cooperative exit")
                qmp.click(240, 940)
                qmp.wait_frame(
                    artifacts / "lifecycle-before-relaunch.ppm",
                    lambda frame: frame[(210 * 480 + 240) * 3:(210 * 480 + 240) * 3 + 3]
                                  == bytes.fromhex("6750a4"),
                    "Home did not settle before checkpoint relaunch")
                qmp.click(96, 410)
                relaunched = guest.wait_lifecycle(calculator_id)["applications"][calculator_id]
                if relaunched["pid"] == calculator_pid:
                    raise RuntimeError("Dismissed task unexpectedly retained its old process")
                calculator_pid = relaunched["pid"]
                retained = {calculator_id: calculator_pid}
                guest.wait_lifecycle(calculator_id, retained)
                qmp.wait_frame(
                    artifacts / "lifecycle-checkpoint-restored.ppm",
                    lambda frame: frame[160 * 480 * 3:220 * 480 * 3]
                                  == calculator[160 * 480 * 3:220 * 480 * 3],
                    "Relaunched calculator lost its checkpointed calculation")
                # Hold the pointer across the status/app input boundary. The
                # shade must follow before release, then snap to stage 1.
                held = qmp.drag(240, 20, 240, 300, artifacts / "shade-dragging.ppm")
                compact = qmp.wait_shade(artifacts / "shade-compact.ppm", 700)
                point = (380 * 480 + 8) * 3
                if (held[point:point + 3] == bytes.fromhex("fff8ff") or
                        compact[point:point + 3] != bytes.fromhex("fff8ff")):
                    raise RuntimeError("Shade did not follow the drag and settle to compact")
                qmp.drag(240, 110, 240, 450)
                expanded = qmp.wait_shade(artifacts / "shade-expanded.ppm", 896)
                point = (820 * 480 + 8) * 3
                if (compact[point:point + 3] == bytes.fromhex("fff8ff") or
                        expanded[point:point + 3] != bytes.fromhex("fff8ff")):
                    raise RuntimeError("Second pull did not expand the shade")
                guest.wait_lifecycle(calculator_id, retained)
                qmp.click(350, 292)  # Manual lock must background the live app
                guest.wait_lifecycle(None, retained, events={calculator_id: "background"})
                qmp.wait_frame(
                    artifacts / "lifecycle-locked-app.ppm",
                    lambda frame: (frame[(200 * 480 + 240) * 3:(200 * 480 + 240) * 3 + 3]
                                   == bytes.fromhex("eaddff") and
                                   frame[100 * 480 * 3:100 * 480 * 3 + 24 * 3]
                                   == bytes.fromhex("fff8ff") * 24),
                    "Manual lock did not settle while the app was backgrounded")
                qmp.click(240, 790)
                guest.wait_lifecycle(None, retained)
                qmp.wait_frame(
                    artifacts / "lifecycle-unlocked-app.ppm",
                    lambda frame: frame[(210 * 480 + 240) * 3:(210 * 480 + 240) * 3 + 3]
                                  == bytes.fromhex("6750a4"),
                    "Unlock did not return home without stopping the calculator")
                qmp.click(96, 410)
                guest.wait_lifecycle(calculator_id, retained)
                qmp.drag(240, 20, 240, 300)
                qmp.wait_shade(artifacts / "lifecycle-reopened-compact.ppm", 700)
                qmp.drag(240, 110, 240, 450)
                qmp.wait_shade(artifacts / "lifecycle-reopened-expanded.ppm", 896)
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
                    print("Target notifications: independent daemon, direct/legacy queue, peer identity, "
                          "12 live notifications, drag scroll and app activation passed")
                    qmp.click(240, 940)
                    time.sleep(.5)
                    qmp.click(96, 410)
                    time.sleep(.5)
                    qmp.drag(240, 20, 240, 300)
                    qmp.wait_shade(artifacts / "shade-return-compact.ppm", 700)
                    qmp.drag(240, 110, 240, 450)
                    qmp.wait_shade(artifacts / "shade-return-expanded.ppm", 896)
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
                def result_region(frame):
                    return b"".join(frame[(y * 480 + 320) * 3:(y * 480 + 444) * 3]
                                    for y in range(160, 220))
                restored = qmp.wait_frame(
                    artifacts / "shade-restored-app.ppm",
                    lambda frame: (result_region(calculator) == result_region(frame) and
                                   frame[(300 * 480 + 8) * 3:(300 * 480 + 8) * 3 + 3]
                                   == bytes.fromhex("fff8ff")),
                    "Closing the shade did not restore the calculator contents and input area")
                processes = json.loads(guest.run("lmpkg running"))
                if processes.get("org.leon.calculator") != calculator_pid:
                    raise RuntimeError("Shade changed the underlying application process")
                guest.wait_lifecycle(calculator_id, retained)
                qmp.click(72, 384)  # Input reaches the restored calculator
                qmp.wait_frame(artifacts / "shade-restored-input.ppm",
                               lambda frame: result_region(frame)!=result_region(restored),
                               "Shade closure did not restore application pointer input")
                qmp.click(240, 940)
                guest.wait_lifecycle(None, retained, events={calculator_id: "background"})
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
                guest.wait_lifecycle("org.leon.settings", retained)
                if guest.run(f"cat /proc/{calculator_pid}/oom_score_adj") != "500":
                    raise RuntimeError("Background calculator retained foreground OOM priority")
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
                qmp.click(400, 720)  # Display -> Reduce motion
                time.sleep(.4)
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("reduce_motion") is not True:
                    raise RuntimeError("Reduce motion preference did not persist")
                qmp.capture(artifacts / "settings-reduced-motion.ppm")
                qmp.click(400, 720)  # Restore full motion
                time.sleep(.4)
                preferences = json.loads(guest.run("cat /data/leon/settings.json"))
                if preferences.get("reduce_motion") is not False:
                    raise RuntimeError("Full motion preference could not be restored")
                qmp.click(36, 75)  # Back to categories
                time.sleep(.4)
                qmp.capture(artifacts / "settings-dark.ppm")
                qmp.click(180, 206)  # Search settings
                for code in "volume":
                    qmp.key(code)
                time.sleep(.5)
                qmp.capture(artifacts / "settings-search.ppm")
                qmp.click(180, 296)  # Matching Sound result
                qmp.wait_frame(
                    artifacts / "settings-sound-ready.ppm",
                    lambda frame: frame[(300 * 480 + 20) * 3:(300 * 480 + 20) * 3 + 3]
                                  == bytes.fromhex("30293c"),
                    "Sound page did not settle at its slider coordinates")
                # Choose a new value so the old shade volume cannot satisfy
                # this check before the settings client's request completes.
                qmp.click(240, 346)  # 50% of the playback slider
                guest.wait_setting("volume", 50)
                qmp.capture(artifacts / "settings-sound.ppm")
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
                if "Volume: 0.50" not in volume:
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
                if args.system_probe:
                    exercise_core_ui(guest,qmp,artifacts,preferences)
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
                qmp.click(80, 940)  # Recent tasks
                container = bytes.fromhex("30293c" if preferences.get("dark") else "eaddff")
                for attempt in range(40):
                    before_tasks = qmp.capture(artifacts / "tasks-before-dismiss.ppm")
                    point = (180 * 480 + 30) * 3
                    if before_tasks[point:point + 3] == container:
                        break
                    time.sleep(.1)
                else:
                    raise RuntimeError("Recent tasks did not finish its entrance")
                guest.wait_lifecycle(None)
                qmp.click(432, 190)  # Dismiss calculator, allow remaining rows to settle
                time.sleep(.12)
                qmp.capture(artifacts / "tasks-dismissing.ppm")
                # Wait for observed final geometry under software emulation.
                for attempt in range(40):
                    after_tasks = qmp.capture(artifacts / "tasks-after-dismiss.ppm")
                    point = (180 * 480 + 30) * 3
                    if after_tasks[point:point + 3] == container:
                        break
                    time.sleep(.1)
                else:
                    raise RuntimeError("Recent task rows did not settle into the dismissed row")
                closed = guest.wait_lifecycle(None, absent=(calculator_id,))
                exits = [record for record in closed["exits"] if record["pid"] == calculator_pid]
                if exits != [{"app": calculator_id, "pid": calculator_pid, "status": 0}]:
                    raise RuntimeError("Recent tasks did not record one cooperative exit: " + str(exits))
                (artifacts / "lifecycle-dismissed.json").write_text(json.dumps(closed, indent=2) + "\n")
                background = bytes.fromhex("15121b" if preferences.get("dark") else "fff8ff")
                primary = bytes.fromhex("d0bcff" if preferences.get("dark") else "6750a4")

                def pixel(frame, x, y):
                    return frame[(y * 480 + x) * 3:(y * 480 + x) * 3 + 3]

                def wait_shade_stage(name, bottom):
                    return qmp.wait_shade(artifacts / name, bottom, preferences.get("dark"))

                qmp.click(400, 22)
                wait_shade_stage("shade-before-lock-compact.ppm", 700)
                qmp.click(400, 22)  # Expand the shade
                wait_shade_stage("shade-before-lock-expanded.ppm", 896)
                qmp.click(350, 292)  # Lock screen tile
                def lock_header(frame):
                    return b"".join(frame[(y * 480 + 24) * 3:(y * 480 + 200) * 3]
                                    for y in range(60, 150))

                # The lock tile changes pages with a spatial transition. Wait
                # for the observed lock geometry before testing blocked input;
                # a fixed delay can still capture the tasks page under TCG.
                locked = qmp.wait_frame(
                    artifacts / "shade-locked.ppm",
                    lambda frame: (lock_header(frame) == background * (176 * 90) and
                                   pixel(frame, 240, 200) == container),
                    "Lock tile did not reach the settled lock screen")
                guest.wait_lifecycle(None)
                qmp.click(400, 22)  # Locked session cannot expose the shade
                time.sleep(.4)
                still_locked = qmp.capture(artifacts / "shade-locked-top.ppm")
                if lock_header(locked) != lock_header(still_locked):
                    raise RuntimeError("Locked session exposed the notification shade")
                qmp.drag(240, 20, 240, 300)
                if lock_header(qmp.capture(artifacts / "shade-locked-pull.ppm")) != lock_header(locked):
                    raise RuntimeError("Pulling the locked status bar changed the session")
                qmp.click(240, 790)
                unlocked = qmp.wait_frame(
                    artifacts / "shade-unlocked-home.ppm",
                    lambda frame: pixel(frame, 240, 210) == primary,
                    "Unlock did not finish the home-page entrance")
                guest.wait_lifecycle(None)
                qmp.click(400, 22)
                wait_shade_stage("shade-before-backdrop.ppm", 700)
                qmp.click(8, 810)  # Dismiss through the backdrop
                qmp.wait_frame(artifacts / "shade-backdrop-dismissed.ppm",
                               lambda frame: lock_header(frame) == lock_header(unlocked),
                               "Shade backdrop did not return to the home screen")
                qmp.click(400, 22)
                wait_shade_stage("shade-before-poweroff.ppm", 700)
                qmp.click(432, 628)
                time.sleep(.3)
                qmp.click(240, 426)  # Explicit power-off choice
                try:
                    proc.wait(timeout=20)
                except subprocess.TimeoutExpired as error:
                    qmp.capture(artifacts / "poweroff-wait.ppm")
                    raise RuntimeError("UI poweroff did not stop QEMU") from error
                if proc.returncode:
                    raise RuntimeError("QEMU exited with status " + str(proc.returncode))
                if "leond: exited compositor status=0 retry=0" not in log.read_text(errors="replace"):
                    raise RuntimeError("Compositor did not shut down cleanly")
                if args.system_probe:
                    verify_core_data(disk,temp)
                if args.terminal_probe:
                    verify_terminal_data(disk, temp)
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
