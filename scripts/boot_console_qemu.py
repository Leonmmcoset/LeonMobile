"""Real VT/fbcon ownership checks; privileged fixture exists only in a cloned disk."""
import json
import re
import time

from PIL import Image


def exercise_boot_console(guest, qmp, artifacts, timeout):
    report = {}

    def rpc(operation):
        output = guest.run("/usr/bin/leon-test-boot-console " + operation)
        # Supervisor lifecycle messages share the serial terminal while the
        # compositor starts/stops; the fixture reply is its own JSON line.
        value = next((json.loads(line) for line in reversed(output.splitlines())
                      if line.startswith("{")), None)
        if value is None:
            raise RuntimeError("Missing boot console fixture reply: " + output)
        if not value.get("ok"):
            raise RuntimeError("Boot console fixture failed: " + operation)
        return value

    def wait(predicate, description):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.15)
        raise RuntimeError(description)

    def pixel(frame, x, y):
        offset = (y * 480 + x) * 3
        return frame[offset:offset + 3]

    def text_frame(frame):
        light = sum(frame[i] > 120 and abs(frame[i] - frame[i + 1]) < 4 and
                    abs(frame[i] - frame[i + 2]) < 4 for i in range(0, len(frame), 48))
        return frame.count(b"\0\0\0") > 100000 and light > 100

    def capture(name, predicate=text_frame):
        path = artifacts / (name + ".ppm")
        frame = qmp.wait_frame(path, predicate, name + " did not render")
        with Image.open(path) as picture:
            picture.save(path.with_suffix(".png"))
        return frame

    def private_region(frame):
        # Exclude the live clock/status bar while retaining the lock hint and
        # the bottom rows where fbcon would draw subsequent log messages.
        return frame[350 * 480 * 3:934 * 480 * 3]

    def locked(name):
        wait(lambda: rpc("state")["mode"] == 1, "Wayland did not obtain KD_GRAPHICS")
        frame = capture(name, lambda f: pixel(f, 0, 100) == bytes.fromhex("fff8ff"))
        stable = 0
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            time.sleep(.15)
            current = qmp.capture(artifacts / (name + ".ppm"))
            stable = stable + 1 if private_region(current) == private_region(frame) else 0
            frame = current
            if stable >= 3:
                with Image.open(artifacts / (name + ".ppm")) as picture:
                    picture.save(artifacts / (name + ".png"))
                return frame
        raise RuntimeError("Lock screen did not settle after VT takeover")

    def text_boot(name):
        state = rpc("state")
        if (state["mode"] != 0 or state["active_vt"] != 1 or
                not all(state[key] for key in ("kernel_text", "init_text", "manager_text"))):
            raise RuntimeError("Kernel/init/service text is missing from the boot VT: " + str(state))
        report[name] = state
        text = guest.run("cat /run/leon/boot-console-text.txt")
        (artifacts / (name + ".txt")).write_text(text + "\n")
        capture(name)

    if ("1000" not in guest.run("id -u").splitlines() or
            "7.3.1-leonmobile" not in guest.run("uname -r").splitlines()):
        raise RuntimeError("Unexpected guest identity or kernel")
    consoles = guest.run("cat /proc/consoles")
    if not all(re.search(rf"^{name}\s", consoles, re.M) for name in ("tty0", "ttyAMA0")):
        raise RuntimeError("Framebuffer and serial kernel consoles must both remain enabled")
    (artifacts / "consoles.txt").write_text(consoles + "\n")
    text_boot("boot-userspace")
    print("PASS real framebuffer kernel/init/leond text, dual consoles and non-root serial login", flush=True)

    rpc("release")
    baseline = locked("boot-wayland-lock")
    rpc("log")
    time.sleep(.5)
    current = qmp.capture(artifacts / "boot-late-log.ppm")
    if rpc("state")["mode"] != 1 or private_region(current) != private_region(baseline):
        raise RuntimeError("A late kernel or supervisor message drew over the lock screen")
    report["late_logs_preserve_graphics"] = True
    print("PASS KD_GRAPHICS takeover and unchanged lock pixels after real kernel/userspace messages", flush=True)

    rpc("stop")
    wait(lambda: rpc("state")["mode"] == 0, "seatd did not restore KD_TEXT after compositor stop")
    rpc("log")
    rpc("state")
    recovered = guest.run("cat /run/leon/boot-console-text.txt")
    if not all(marker in recovered for marker in ("LEON_BOOT_KERNEL_DIAGNOSTIC", "LEON_BOOT_USERSPACE_DIAGNOSTIC")):
        raise RuntimeError("Restored text console did not receive fresh diagnostics")
    capture("boot-text-recovery")
    (artifacts / "boot-text-recovery.txt").write_text(recovered + "\n")
    rpc("start")
    locked("boot-restored-lock")
    report["stop_restores_text"] = True
    print("PASS compositor stop restores actual text output and restart restores the lock screen", flush=True)

    before = guest.run("leonctl status compositor")
    old_pid = re.search(r"pid=(\d+)", before)[1]
    rpc("crash")  # The fixture confirms KD_TEXT before its reply.
    wait(lambda: bool(re.search(r"compositor\trunning\tpid=(?!" + old_pid + r"\b)\d+",
                               guest.run("leonctl status compositor"))),
         "The killed compositor did not restart with a new PID")
    locked("boot-crash-restored-lock")
    report["crash_restores_text_and_restarts"] = True
    print("PASS SIGKILL releases the graphics VT and supervised recovery returns to a locked scene", flush=True)

    start = guest.log.stat().st_size
    guest.run("lmsys power '{\"op\":\"reboot\"}'")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with guest.log.open("rb") as stream:
            stream.seek(start)
            output = stream.read().decode(errors="replace").replace("\r", "")
        if "leond: started boot-console-test" in output and "\n$ " in output:
            break
        time.sleep(.2)
    else:
        raise RuntimeError("Real reboot did not return to the boot console fixture")
    text_boot("boot-reboot-userspace")
    rpc("release")
    locked("boot-reboot-lock")
    qmp.click(240, 790)
    guest.wait_lifecycle("")
    qmp.wait_frame(artifacts / "boot-final-home.ppm",
                   lambda f: pixel(f, 40, 250) == bytes.fromhex("6750a4"),
                   "Desktop did not accept input after real reboot")
    report["real_reboot_and_desktop_input"] = True
    (artifacts / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS real reboot repeats text-to-Wayland handoff and desktop input remains functional", flush=True)
