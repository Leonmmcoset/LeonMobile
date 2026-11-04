"""Run under the independent Weston smoke-test server."""
import os
from pathlib import Path
import socket
import subprocess

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LEON_TEST_BIN", repo / "build/host"))
parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
parent.settimeout(10)
proc = subprocess.Popen([build / "bin/lmui-wayland-lifecycle-test"], pass_fds=[child.fileno()],
                        env=dict(os.environ, LEON_LIFECYCLE_FD=str(child.fileno())),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
child.close()
try:
    for sequence, event in enumerate(("foreground", "background", "save", "shutdown"), 1):
        parent.send(f"{sequence} {event}".encode())
        assert parent.recv(64) == f"{sequence} ok".encode()
    out, error = proc.communicate(timeout=5)
    assert proc.returncode == 0, error
    assert out.splitlines() == ["0", "1", "2", "3"], out
    print("Wayland lifecycle: foreground/background/save/shutdown callbacks acknowledged")
finally:
    parent.close()
    if proc.poll() is None:
        proc.kill()
        proc.communicate()
