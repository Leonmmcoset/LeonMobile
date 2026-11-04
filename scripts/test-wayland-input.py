#!/usr/bin/env python3
"""Drive LMUI through an independent X11 Weston seat, with real key/clipboard events."""
import os
import argparse
from pathlib import Path
import select
import subprocess
import tempfile
import time

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LEON_TEST_BIN", repo / "build/host"))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--terminal", action="store_true", help="exercise the terminal with real shell, vi and clipboard")
args = parser.parse_args()
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
        env["HOME"] = str(runtime)
        client = subprocess.Popen([build / "bin" / ("leon-terminal" if args.terminal else "lmui-wayland-input-test")], env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        window = subprocess.check_output(["xdotool", "search", "--sync", "--class", "weston"],
                                         env=env, timeout=10).decode().splitlines()[-1]
        def action(*args):
            subprocess.run(["xdotool", *args], env=env, check=True, timeout=5)
        time.sleep(.6)
        action("mousemove", "--window", window, "240", "20" if args.terminal else "300", "click", "1")
        if args.terminal:
            def command(text):
                action("type", "--clearmodifiers", "--delay", "2", text)
                action("key", "Return")
            def wait_file(name, expected):
                deadline = time.monotonic() + 8
                target = runtime / name
                while time.monotonic() < deadline:
                    if target.exists() and target.read_text() == expected:
                        return
                    if client.poll() is not None:
                        raise RuntimeError("Terminal exited: " + client.stderr.read())
                    time.sleep(.05)
                raise RuntimeError("Terminal did not produce " + name + ": " +
                                   (target.read_text() if target.exists() else "missing"))
            command("printf '%s' ready > ready.txt")
            wait_file("ready.txt", "ready")
            command("sleep 60")
            time.sleep(.3)
            action("key", "ctrl+c")
            command("printf '%s' interrupted > interrupt.txt")
            wait_file("interrupt.txt", "interrupted")
            command("vi edited.txt")
            time.sleep(.5)
            action("type", "--clearmodifiers", "--delay", "20", "iLeonMobile terminal")
            action("key", "Escape")
            command(":wq")
            wait_file("edited.txt", "LeonMobile terminal\n")
            time.sleep(.5)
            command("printf '\\033[2J\\033[HCLIP_TEXT\\n'; printf '%s' done > cleared.txt")
            wait_file("cleared.txt", "done")
            time.sleep(.4)
            action("mousemove", "--window", window, "56", "904", "click", "1")
            time.sleep(.15)
            action("mousemove", "--window", window, "27", "107", "mousedown", "1")
            time.sleep(.1)
            action("mousemove", "--window", window, "91", "107")
            time.sleep(.15)
            action("mouseup", "1")
            time.sleep(.15)
            action("mousemove", "--window", window, "130", "904", "click", "1")
            time.sleep(.2)
            command("read value; printf '%s' \"$value\" > pasted.txt")
            action("key", "ctrl+shift+v")
            time.sleep(.3)
            action("key", "Return")
            wait_file("pasted.txt", "CLIP_TEXT")
            command("exit 7")
            time.sleep(.4)
            if client.poll() is not None:
                raise RuntimeError("Exited shell unexpectedly closed the app")
            action("mousemove", "--window", window, "425", "904", "click", "1")
            time.sleep(.3)
            command("printf '%s' restarted > restart.txt")
            wait_file("restart.txt", "restarted")
            print("Wayland terminal: real shell, Ctrl-C, vi save, selection/clipboard and session restart passed")
            client.terminate()
            client.communicate(timeout=5)
        else:
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
        if args.terminal:
            from PIL import ImageGrab
            ImageGrab.grab(xdisplay=display).save(repo / "build/screenshots/terminal-native-failed.png")
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
