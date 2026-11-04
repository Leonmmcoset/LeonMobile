#!/usr/bin/env python3
"""Drive LMUI through an independent X11 Weston seat, with real key/clipboard events."""
import os
from pathlib import Path
import select
import subprocess
import tempfile
import time

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LEON_TEST_BIN", repo / "build/host"))
with tempfile.TemporaryDirectory(prefix="leon-input-") as directory:
    runtime = Path(directory)
    read_fd, write_fd = os.pipe()
    xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "640x1100x24"],
                            pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    os.close(write_fd)
    weston = client = None
    try:
        if not select.select([read_fd], [], [], 10)[0]:
            raise RuntimeError("Xvfb did not allocate a display")
        display = ":" + os.read(read_fd, 64).decode().strip()
        env = dict(os.environ, DISPLAY=display, XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="input-test")
        weston = subprocess.Popen(["weston", "--backend=x11-backend.so", "--use-pixman",
                                   "--socket=input-test", "--width=480", "--height=960",
                                   "--idle-time=0", "--shell=kiosk-shell.so",
                                   "--log=" + str(runtime / "weston.log")], env=env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 10
        while not (runtime / "input-test").exists() and time.monotonic() < deadline:
            if weston.poll() is not None:
                raise RuntimeError((runtime / "weston.log").read_text())
            time.sleep(.05)
        client = subprocess.Popen([build / "bin/lmui-wayland-input-test"], env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        window = subprocess.check_output(["xdotool", "search", "--sync", "--class", "weston"],
                                         env=env, timeout=10).decode().splitlines()[-1]
        def action(*args):
            subprocess.run(["xdotool", *args], env=env, check=True, timeout=5)
        time.sleep(.6)
        action("mousemove", "--window", window, "240", "300", "click", "1")
        for key in ("ctrl+k", "alt+a", "shift+Right", "c", "v"):
            action("key", key)
            time.sleep(.2)
        action("keydown", "x")
        time.sleep(1)
        action("keyup", "x")
        time.sleep(.2)
        action("key", "Return")
        out, error = client.communicate(timeout=8)
        if client.returncode:
            raise RuntimeError(error)
        print(out.strip())
    except Exception:
        if (runtime / "weston.log").exists():
            print((runtime / "weston.log").read_text(), flush=True)
        raise
    finally:
        os.close(read_fd)
        for process in (client, weston, xvfb):
            if process and process.poll() is None:
                process.terminate()
            if process:
                process.communicate(timeout=5)
