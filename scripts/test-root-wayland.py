#!/usr/bin/env python3
"""Exercise the trusted Root UI through an independent Weston seat (no Leon compositor)."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
from PIL import ImageGrab

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tests"))
from test_root_applications import RootApplications, SECRET, BUILD


def wait(predicate, message, timeout=8):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate(): return
        time.sleep(.05)
    raise RuntimeError(message)


def main():
    fixture = RootApplications()
    fixture.setUp()
    xvfb = weston = shell = None
    artifacts = REPO / "build/screenshots"
    artifacts.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="leon-root-seat-") as name:
            runtime = Path(name)
            read_fd, write_fd = os.pipe()
            xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "640x1100x24"],
                                    pass_fds=(write_fd,), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            os.close(write_fd)
            if not select.select([read_fd], [], [], 10)[0]: raise RuntimeError("Xvfb startup failed")
            display = ":" + os.read(read_fd, 64).decode().strip()
            os.close(read_fd)
            env = dict(os.environ, DISPLAY=display, XDG_RUNTIME_DIR=name, WAYLAND_DISPLAY="root-seat",
                       LEON_ROOT_SOCKET=str(fixture.endpoint), LEON_AUTH_SOCKET=str(fixture.auth_socket),
                       HOME=name, LMUI_CAPTURE_FRAME=str(artifacts / "root-native-initial.png"),
                       LEON_SHELL_CAPTURE_SHADE="compact")
            weston = subprocess.Popen(["weston", "--backend=x11-backend.so", "--use-pixman",
                "--socket=root-seat", "--width=480", "--height=960", "--idle-time=0",
                "--shell=kiosk-shell.so", "--log=" + str(runtime / "weston.log")], env=env,
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            wait(lambda: (runtime / "root-seat").exists(), "Weston startup failed")
            shell = subprocess.Popen([BUILD / "bin/leon-shell"], env=env,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            window = subprocess.check_output(["xdotool", "search", "--sync", "--class", "weston"],
                                             env=env, timeout=10).decode().splitlines()[-1]
            def action(*args):
                subprocess.run(["xdotool", *args], env=env, check=True, timeout=5)
            def click(x, y):
                action("mousemove", "--window", window, str(x), str(y), "click", "1")
                time.sleep(.2)
            def snapshot(name):
                ImageGrab.grab(xdisplay=display).save(artifacts / (name + ".png"))
            def prompt_bound():
                for p in fixture.state.glob("request-*.json"):
                    try:
                        if json.loads(p.read_text()).get("ui_pid") == shell.pid: return True
                    except (OSError, ValueError): pass
                return False
            time.sleep(.8)
            click(240, 20)
            child, first = fixture.request()
            wait(prompt_bound, "Shell did not fetch the installed application's request")
            time.sleep(.4)
            snapshot("root-native-details")
            click(240, 748)
            action("type", "--clearmodifiers", "--delay", "5", "incorrect-root")
            snapshot("root-native-password-masked")
            action("key", "Return")
            wait(lambda: json.loads((fixture.state / "policy.json").read_text())["failures"] == 1,
                 "Wrong password was not checked by rootd")
            time.sleep(1.2)
            action("type", "--clearmodifiers", "--delay", "5", SECRET)
            action("key", "Return")
            output, frames = fixture.complete(child)
            assert b"helper_uid=0" in output and frames[-2]["status"] == 7
            snapshot("root-native-complete")
            child, first = fixture.request()
            wait(prompt_bound, "Second command skipped a new system confirmation")
            time.sleep(.3)
            click(240, 814)
            fixture.complete(child)
            assert fixture.call("app_list")["apps"][0]["blocked"]
            snapshot("root-native-blocked")
            # A stopped rootd must not freeze the Wayland event loop or leave private input.
            fixture.process.send_signal(19)
            action("key", "Escape")
            time.sleep(.6)
            fixture.process.send_signal(18)
            assert shell.poll() is None
            print("Independent Weston Root UI: exact command, masked password, failed authentication, fresh approval, denial/block and service stall passed")
    finally:
        for process in (shell, weston, xvfb):
            if process:
                if process.poll() is None: process.terminate()
                try: process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.communicate(timeout=5)
        if fixture.process and fixture.process.poll() is None: fixture.process.send_signal(18)
        fixture.tearDown()

if __name__ == "__main__":
    main()
