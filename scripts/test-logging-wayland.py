#!/usr/bin/env python3
"""Exercise diagnostics with real logd/authd/crashd/leond on independent Weston."""
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import tempfile
import time

from PIL import ImageGrab

REPO = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host")) / "bin"
ARTIFACTS = REPO / "build/logging-wayland"
ARTIFACTS.mkdir(parents=True, exist_ok=True)


def rpc(path, request):
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(4)
        client.connect(str(path))
        client.sendall(json.dumps(request).encode() + b"\n")
        return json.loads(client.makefile("rb").readline())


def wait(predicate, description):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (OSError, ValueError):
            pass
        time.sleep(0.05)
    raise RuntimeError(description)


with tempfile.TemporaryDirectory(prefix="leon-log-ui-") as directory:
    runtime = Path(directory)
    processes = []
    evidence = open(ARTIFACTS / "processes.log", "wb")
    def start(args, **kwargs):
        process = subprocess.Popen(args, stdout=evidence, stderr=evidence, **kwargs)
        processes.append(process)
        return process
    try:
        reader, writer = os.pipe()
        xvfb = start(["Xvfb", "-displayfd", str(writer), "-screen", "0", "640x1100x24"],
                      pass_fds=[writer])
        os.close(writer)
        if not select.select([reader], [], [], 10)[0]:
            raise RuntimeError("Xvfb did not allocate a display")
        display = ":" + os.read(reader, 64).decode().strip()
        os.close(reader)
        env = dict(os.environ, DISPLAY=display, XDG_RUNTIME_DIR=str(runtime),
                   WAYLAND_DISPLAY="diagnostics-test", HOME=str(runtime),
                   LEON_AUTH_SOCKET=str(runtime / "auth.sock"),
                   LEON_LOG_SOCKET=str(runtime / "logs/control.sock"))
        weston = start(["weston", "--backend=x11-backend.so", "--use-pixman",
            "--socket=diagnostics-test", "--width=480", "--height=960", "--idle-time=0",
            "--shell=kiosk-shell.so", "--log=" + str(ARTIFACTS / "weston.log")], env=env)
        wait(lambda: (runtime / "diagnostics-test").exists(), "Weston unavailable")
        authd = start([BIN / "authd", "--test", "--socket", runtime / "auth.sock",
                       "--state-dir", runtime / "auth-state"])
        wait(lambda: (runtime / "auth.sock").exists(), "authd unavailable")
        token = rpc(runtime / "auth.sock", {"op": "authenticate"})["token"]
        assert rpc(runtime / "auth.sock", {"op": "consume", "purpose": "unlock", "token": token})["ok"]
        config = runtime / "services"
        config.mkdir()
        (config / "sleep.toml").write_text('name="diagnostics-sample"\ncommand=["/bin/sleep","600"]\n')
        leond = start([BIN / "leond", "--config", config, "--socket", runtime / "services.sock",
                       "--log-dir", runtime / "legacy"])
        crashd = start([BIN / "crashd", "--test", "--socket", runtime / "crash.sock",
                        "--state", runtime / "crashes"])
        logd_args = [BIN / "logd", "--test", "--runtime", runtime / "logs",
                    "--state", runtime / "journal", "--auth", runtime / "auth.sock",
                    "--crash", runtime / "crash.sock", "--services", runtime / "services.sock"]
        logd = start(logd_args)
        wait(lambda: (runtime / "logs/capture.sock").exists(), "logd unavailable")
        wait(lambda: (runtime / "crash.sock").exists(), "crashd unavailable")
        # Real root supervisor datagram; printable text plus malicious ESC/bidi bytes.
        message = b"DIAGNOSTIC_TEST secret-marker \x1b[2J " + "中文\u202e".encode()
        packet = struct.pack("=6I96s1024s", 0x4c4d4c31, 12000, 123, 4, 1,
                             len(message), b"diagnostics-sample", message)
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as producer:
            producer.sendto(packet, str(runtime / "logs/capture.sock"))
        wait(lambda: rpc(runtime / "logs/control.sock", {"op": "query"})["records"], "No log record")
        settings = start([BIN / "leon-settings"], env=env)
        window = subprocess.check_output(["xdotool", "search", "--sync", "--class", "weston"],
                                         env=env, timeout=10).decode().splitlines()[-1]
        def action(*args):
            subprocess.run(["xdotool", *args], env=env, check=True, timeout=5)
        def click(x, y):
            action("mousemove", "--window", window, str(x), str(y), "click", "1")
            time.sleep(0.4)
        def capture(name):
            shot = ImageGrab.grab(xdisplay=display)
            shot.save(ARTIFACTS / (name + ".png"))
            return shot
        def row_present():
            return ImageGrab.grab(xdisplay=display).getpixel((22, 510))[:3] == (255, 251, 255)
        time.sleep(0.7)
        click(180, 164)
        action("type", "--delay", "30", "diagnostics")
        action("key", "Return")
        time.sleep(0.3)
        click(180, 254)
        wait(row_present, "Settings did not show an authorized system log record")
        capture("logs")
        # The record opens a detail page containing boot/UID/PID and full escaped text.
        click(160, 510)
        detail = capture("detail")
        assert detail.getpixel((22, 510))[:3] != (255, 251, 255), "Log row did not open details"
        click(36, 33)
        click(240, 318)
        time.sleep(0.8)
        capture("services")
        click(390, 318)
        time.sleep(0.8)
        capture("crashes")
        click(80, 318)
        time.sleep(0.8)
        click(160, 510)
        before = capture("before-auth-failure")
        os.kill(authd.pid, signal.SIGSTOP)
        time.sleep(0.8)
        revoked = capture("auth-failure-clears-detail")
        assert before.crop((20, 300, 470, 750)).tobytes() != revoked.crop((20, 300, 470, 750)).tobytes(), "Auth outage retained the private detail"
        os.kill(authd.pid, signal.SIGCONT)
        click(110, 375)
        time.sleep(0.8)
        logd.terminate()
        logd.wait(timeout=4)
        started = time.monotonic()
        click(110, 375)
        click(36, 33)
        assert time.monotonic() - started < 2, "Unavailable logger blocked navigation"
        capture("service-unavailable-back")
        assert settings.poll() is None, "Logging failure crashed Settings"
        logd = start(logd_args)
        wait(lambda: rpc(runtime / "logs/control.sock", {"op": "status"})["ok"], "Logger failed to restart")
        click(180, 254)
        time.sleep(1)
        capture("recovered")
        print("Independent Weston diagnostics: logs/details, service/crash tabs, auth outage clearing, logger restart and responsive Back passed")
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                os.kill(process.pid, signal.SIGCONT)
                process.terminate()
            try:
                process.wait(timeout=6)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)
        evidence.close()
