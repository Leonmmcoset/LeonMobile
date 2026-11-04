"""Developer root UI and reboot checks used by test-qemu.py on a disposable disk."""
import json
import time

PASSWORD = "LeonNext!2026"


def wait_probe(guest, artifacts, phase):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        result = guest.run("cat /run/leon/root-tests.txt 2>/dev/null || true")
        if "ROOT_TESTS_FAILED" in result:
            raise RuntimeError("Root target probe failed:\n" + result)
        if f"ROOT_TESTS_PASSED phase={phase}" in result:
            (artifacts / f"root-phase-{phase}.txt").write_text(result + "\n")
            print(result, flush=True)
            return
        time.sleep(.2)
    raise RuntimeError("Root target probe timed out:\n" + result)


def reboot_and_probe(guest, artifacts, phase, timeout):
    # The guest kernel, init and data filesystem really restart; QEMU stays alive.
    start = guest.log.stat().st_size
    guest.run("lmsys power '{\"op\":\"reboot\"}'")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with guest.log.open("rb") as stream:
            stream.seek(start)
            text = stream.read().decode(errors="replace").replace("\r", "")
        if "LeonMobile 1.0.0: init ready" in text and "leond: started shell" in text and "\n$ " in text:
            break
        if guest.proc.poll() is not None:
            raise RuntimeError("QEMU exited during reboot")
        time.sleep(.2)
    else:
        raise RuntimeError("Guest reboot timed out")
    if guest.run("id -u") != "1000":
        raise RuntimeError("Reboot changed serial console identity")
    wait_probe(guest, artifacts, phase)


def verify_console(guest):
    guest.run("su --status | grep '\"enabled\":true'")
