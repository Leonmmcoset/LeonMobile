#!/usr/bin/env python3
"""Drive LMUI through an independent X11 Weston seat, with real key/clipboard events."""
import os
import argparse
from pathlib import Path
import select
import subprocess
import tempfile
import time
import json
import socket
import sys
import signal

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LEON_TEST_BIN", repo / "build/host"))
parser = argparse.ArgumentParser(description=__doc__)
mode = parser.add_mutually_exclusive_group()
mode.add_argument("--security", action="store_true", help="exercise real device credential setup, rotation and removal")
mode.add_argument("--terminal", action="store_true", help="exercise the terminal with real shell, vi and clipboard")
mode.add_argument("--developer", action="store_true", help="exercise Settings root setup and a private kernel fixture")
mode.add_argument("--applications", action="store_true", help="exercise Settings app management with real broker/portal fixtures")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="leon-input-") as directory:
    runtime = Path(directory)
    read_fd, write_fd = os.pipe()
    xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "640x1100x24"],
                            pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    os.close(write_fd)
    weston = client = rootd = authd = None
    app_fixture = None
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
        if args.developer:
            (runtime / "proc/sys/kernel").mkdir(parents=True)
            (runtime / "proc/sys/kernel/hostname").write_text("before\n")
            (runtime / "modules").mkdir()
            (runtime / "services").mkdir()
            env["LEON_ROOT_SOCKET"] = str(runtime / "root.sock")
            rootd = subprocess.Popen([build / "bin/rootd", "--test", "--state-dir", runtime / "state",
                                      "--socket", env["LEON_ROOT_SOCKET"], "--proc", runtime / "proc",
                                      "--modules", runtime / "modules", "--services", runtime / "services"],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            deadline = time.monotonic() + 5
            while not (runtime / "root.sock").exists() and time.monotonic() < deadline:
                if rootd.poll() is not None: raise RuntimeError(rootd.stderr.read().decode())
                time.sleep(.02)
        if args.security:
            env["LEON_AUTH_SOCKET"] = str(runtime / "auth/control.sock")
            authd = subprocess.Popen([build / "bin/authd", "--test", "--state-dir", runtime / "auth-state",
                                      "--socket", env["LEON_AUTH_SOCKET"]], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            deadline = time.monotonic() + 5
            while not Path(env["LEON_AUTH_SOCKET"]).exists() and time.monotonic() < deadline:
                if authd.poll() is not None: raise RuntimeError(authd.stderr.read().decode())
                time.sleep(.02)
            def auth_call(**request):
                with socket.socket(socket.AF_UNIX) as connection:
                    connection.settimeout(4)
                    connection.connect(env["LEON_AUTH_SOCKET"])
                    connection.sendall(json.dumps(request).encode() + b'\n')
                    return json.loads(connection.makefile('rb').readline())
            token = auth_call(op='authenticate')['token']
            assert auth_call(op='consume', purpose='unlock', token=token)['ok']
        if args.applications:
            sys.path.insert(0, str(repo / "tests"))
            from test_app_management import AppManagementTests
            app_fixture = AppManagementTests()
            app_fixture.setUp()
            app_fixture.launch()
            app_fixture.grant()
            managed_data = app_fixture.root / "data/org.leon.gallery/private"
            managed_data.write_text("native UI user data")
            env["LEON_APPS_SOCKET"] = str(app_fixture.apps_endpoint)
        executable = "leon-terminal" if args.terminal else "leon-settings" if args.developer or args.security or args.applications else "lmui-wayland-input-test"
        client = subprocess.Popen([build / "bin" / executable], env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        window = subprocess.check_output(["xdotool", "search", "--sync", "--class", "weston"],
                                         env=env, timeout=10).decode().splitlines()[-1]
        def action(*args):
            subprocess.run(["xdotool", *args], env=env, check=True, timeout=5)
        time.sleep(.6)
        action("mousemove", "--window", window, "240", "20" if args.terminal or args.developer or args.security or args.applications else "300", "click", "1")
        if args.applications:
            from PIL import ImageGrab
            (repo / "build/screenshots").mkdir(parents=True, exist_ok=True)
            def click(x, y):
                action("mousemove", "--window", window, str(x), str(y), "click", "1")
                time.sleep(.4)
            def type_value(value):
                action("type", "--clearmodifiers", "--delay", "5", value)
                action("key", "Return")
                time.sleep(.3)
            def wait_for(check, description):
                deadline = time.monotonic() + 6
                while time.monotonic() < deadline:
                    if check(): return
                    if client.poll() is not None: raise RuntimeError(client.stderr.read())
                    time.sleep(.05)
                raise RuntimeError(description)
            def capture(name):
                time.sleep(.3)
                ImageGrab.grab(xdisplay=display).save(repo / "build/screenshots" / (name + ".png"))
            click(180, 164); type_value("Apps"); click(180, 254)
            capture("applications-native-list")
            click(150, 188); type_value("Gallery"); click(180, 324)
            capture("applications-native-detail")
            click(90, 416)
            wait_for(lambda: not app_fixture.info()["running"], "Settings Stop did not stop app")
            assert managed_data.exists(), "Stop must retain private data"
            click(240, 416)
            capture("applications-native-clear-confirm")
            click(120, 512)
            assert managed_data.exists(), "Cancel clear erased data"
            click(240, 416); click(360, 512)
            wait_for(lambda: not managed_data.exists(), "Settings clear did not erase private data")
            wait_for(lambda: app_fixture.info()["permissions"][0]["state"] == "not-requested",
                     "Settings clear did not reset grants")
            capture("applications-native-cleared")
            click(36, 33)
            click(150, 188)
            for _ in "Gallery": action("key", "BackSpace")
            type_value("Demo"); click(180, 324)
            capture("applications-native-local")
            click(390, 416); click(120, 512)
            assert app_fixture.call("app_info", app_fixture.local)["ok"], "Cancel uninstall removed app"
            click(390, 416); click(360, 512)
            wait_for(lambda: not app_fixture.call("app_info", app_fixture.local)["ok"],
                     "Settings uninstall did not remove local app")
            capture("applications-native-uninstalled")
            # A blocked broker must not freeze Settings through its usual
            # synchronous preference poll while an async management job waits.
            os.kill(app_fixture.broker.pid, signal.SIGSTOP)
            try:
                started = time.monotonic()
                click(420, 188)
                click(36, 33)
                shot = ImageGrab.grab(xdisplay=display)
                assert shot.getpixel((30, 156))[:3] == (234, 221, 255), "Settings froze during broker stall"
                assert time.monotonic() - started < 2, "Settings navigation stalled with broker"
                shot.save(repo / "build/screenshots/applications-native-stalled-responsive.png")
            finally:
                os.kill(app_fixture.broker.pid, signal.SIGCONT)
            time.sleep(.4)
            click(180, 254)
            broker_args = app_fixture.broker.args
            app_fixture.broker.terminate(); app_fixture.broker.communicate(timeout=5)
            click(420, 188)
            capture("applications-native-service-unavailable")
            assert client.poll() is None, "Broker failure crashed Settings"
            app_fixture.broker = app_fixture.start(broker_args, app_fixture.apps_endpoint)
            click(420, 188)
            capture("applications-native-service-recovered")
            print("Independent Weston: app search/details, stop, cancel/confirm clear and uninstall, permission reset, broker stall responsiveness and failure/retry passed")
        elif args.security:
            def click(x, y):
                action("mousemove", "--window", window, str(x), str(y), "click", "1")
                time.sleep(.35)
            def enter(value):
                action("type", "--clearmodifiers", "--delay", "5", value)
                action("key", "Return")
                time.sleep(.25)
            def wait_kind(kind):
                deadline = time.monotonic() + 6
                while time.monotonic() < deadline:
                    status = auth_call(op='status')
                    if status['kind'] == kind: return
                    time.sleep(.05)
                raise RuntimeError("Settings did not change credential kind: " + str(status))
            click(180, 590)
            click(180, 454)
            click(180, 328)
            enter('739182'); enter('739182')
            wait_kind('pin')
            from PIL import ImageGrab
            (repo / "build/screenshots").mkdir(parents=True, exist_ok=True)
            ImageGrab.grab(xdisplay=display).save(repo / "build/screenshots/security-native-pin.png")
            click(180, 400)
            for value in ('739182', 'Device passphrase 2026!', 'Device passphrase 2026!'): enter(value)
            wait_kind('password')
            click(180, 472)
            enter('wrong-password')
            deadline = time.monotonic() + 5
            while not auth_call(op='status')['retry_after_ms'] and time.monotonic() < deadline: time.sleep(.02)
            assert auth_call(op='status')['kind'] == 'password'
            time.sleep(1.2)
            click(180, 472)
            enter('Device passphrase 2026!')
            wait_kind('none')
            policy = (runtime / 'auth-state/policy.json').read_text()
            assert '739182' not in policy and 'Device passphrase 2026!' not in policy
            ImageGrab.grab(xdisplay=display).save(repo / "build/screenshots/security-native-disabled.png")
            print("Independent Weston: Settings PIN setup, password rotation, incorrect credential delay and authenticated removal passed")
        elif args.developer:
            password = "NativeRoot!2026"
            def click(x, y):
                action("mousemove", "--window", window, str(x), str(y), "click", "1")
                time.sleep(.35)
            def type_value(value):
                action("type", "--clearmodifiers", "--delay", "5", value)
                action("key", "Return")
                time.sleep(.3)
            def wait_state(enabled):
                deadline = time.monotonic() + 6
                result = {}
                while time.monotonic() < deadline:
                    with socket.socket(socket.AF_UNIX) as connection:
                        connection.connect(env["LEON_ROOT_SOCKET"])
                        connection.sendall(b'{"op":"status"}\n')
                        result = json.loads(connection.makefile("rb").readline())
                    if result.get("ok") and result.get("enabled") == enabled: return
                    time.sleep(.05)
                raise RuntimeError("Settings root toggle failed: " + str(result))
            click(180, 590)  # System
            click(180, 370)  # Developer options, visible without an unlock gesture
            wait_state(False)
            click(400, 215)
            type_value(password)
            type_value(password)
            wait_state(True)
            from PIL import ImageGrab
            (repo / "build/screenshots").mkdir(parents=True, exist_ok=True)
            ImageGrab.grab(xdisplay=display).save(repo / "build/screenshots/developer-native-enabled.png")
            click(180, 390)  # Runtime kernel parameters
            click(180, 298)  # kernel/
            click(180, 298)  # hostname
            click(180, 320)
            action("key", "BackSpace", "BackSpace", "BackSpace", "BackSpace", "BackSpace", "BackSpace")
            type_value("native-root")
            click(180, 384)
            type_value(password)
            deadline = time.monotonic() + 5
            parameter = runtime / "proc/sys/kernel/hostname"
            while parameter.read_text() != "native-root\n" and time.monotonic() < deadline: time.sleep(.05)
            if parameter.read_text() != "native-root\n":
                raise RuntimeError("Settings did not write the private sysctl fixture")
            for _ in range(3): click(60, 186)
            click(400, 215)
            type_value(password)
            wait_state(False)
            click(400, 215)
            type_value(password)
            wait_state(True)
            generation = json.loads((runtime / "state/policy.json").read_text())["generation"]
            click(180, 314)  # Change password: old, new, confirmation
            for value in (password, "RotatedRoot!2026", "RotatedRoot!2026"): type_value(value)
            deadline = time.monotonic() + 6
            while json.loads((runtime / "state/policy.json").read_text())["generation"] == generation and time.monotonic() < deadline:
                time.sleep(.05)
            time.sleep(.2)
            click(400, 215)
            type_value("RotatedRoot!2026")
            wait_state(False)
            policy = (runtime / "state/policy.json").read_text()
            if password in policy or "RotatedRoot!2026" in policy:
                raise RuntimeError("Settings persisted a plaintext password")
            ImageGrab.grab(xdisplay=display).save(repo / "build/screenshots/developer-native-disabled.png")
            client.terminate(); client.communicate(timeout=5)
            print("Wayland Settings: first password setup, kernel fixture edit, disable/re-enable and password rotation passed")
        elif args.terminal:
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
        if args.terminal or args.developer or args.security or args.applications:
            from PIL import ImageGrab
            (repo / "build/screenshots").mkdir(parents=True, exist_ok=True)
            ImageGrab.grab(xdisplay=display).save(repo / "build/screenshots/input-native-failed.png")
        if (runtime / "weston.log").exists():
            print((runtime / "weston.log").read_text(), flush=True)
        raise
    finally:
        os.close(read_fd)
        for process in (client, rootd, authd, weston, xvfb):
            if process and process.poll() is None:
                process.terminate()
            if process:
                process.communicate(timeout=5)
        if app_fixture:
            if not app_fixture.doCleanups():
                raise RuntimeError("Application management fixture cleanup failed")
