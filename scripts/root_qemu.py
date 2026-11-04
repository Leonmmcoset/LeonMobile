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


def exercise_developer_ui(guest, qmp, artifacts):
    def click(x, y):
        qmp.click(x, y)
        time.sleep(.4)

    def state():
        return json.loads(guest.run("su --status"))

    def wait_enabled(enabled):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            current = state()
            if current.get("enabled") == enabled:
                return
            time.sleep(.15)
        qmp.capture(artifacts / "developer-failed.ppm")
        raise RuntimeError("Developer root switch did not change policy: " + str(current))

    def authenticate():
        qmp.type_text(PASSWORD)
        qmp.key("ret")
        time.sleep(.7)

    verify_console(guest)
    click(400, 22)
    click(372, 628)  # Settings from compact quick settings
    guest.wait_lifecycle("org.leon.settings")
    qmp.wait_frame(artifacts / "developer-settings-ready.ppm",
                   lambda frame: frame[(196 * 480 + 80) * 3:(196 * 480 + 80) * 3 + 3]
                                 == bytes.fromhex("eaddff"),
                   "Settings did not render before developer navigation")
    click(180, 632)  # System
    click(180, 412)  # Developer options
    qmp.capture(artifacts / "developer-enabled.ppm")
    click(400, 257)
    qmp.wait_frame(artifacts / "developer-password-keyboard.ppm",
                   lambda frame: frame[(604 * 480 + 3) * 3:(604 * 480 + 3) * 3 + 3]
                                 == bytes.fromhex("eaddff"),
                   "Password field did not request the shared IME")
    # The IME starts in Chinese mode. Password purpose must commit the capital
    # letter directly, with no pinyin preedit or candidate substitution.
    click(28, 805)  # Shift
    click(444, 754)  # L
    click(28, 805)
    qmp.type_text(PASSWORD[1:])
    qmp.capture(artifacts / "developer-password-masked.ppm")
    qmp.key("ret")
    wait_enabled(False)
    qmp.capture(artifacts / "developer-disabled.ppm")
    click(400, 257)
    authenticate()
    wait_enabled(True)

    click(180, 430)  # Kernel directory, read-only inspection
    qmp.capture(artifacts / "developer-kernel.ppm")
    click(60, 228)
    click(180, 510)  # Modules
    qmp.capture(artifacts / "developer-modules-loaded.ppm")
    click(180, 336)  # Available catalog
    qmp.capture(artifacts / "developer-modules-available.ppm")
    click(60, 228)
    click(180, 590)  # Service list
    names = [line.split("\t")[0] for line in guest.run("leonctl status").splitlines()]
    index = names.index("notifications")
    if index >= 8:
        raise RuntimeError("Notification service moved outside the first visible page")
    click(180, 340 + index * 64)
    qmp.capture(artifacts / "developer-service.ppm")
    old = guest.run("pidof notificationd")
    click(390, 444)  # Restart and authenticate
    authenticate()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        current = guest.run("pidof notificationd || true")
        if current and current != old:
            break
        time.sleep(.2)
    else:
        qmp.capture(artifacts / "developer-service-failed.ppm")
        raise RuntimeError("Settings did not restart notificationd")
    click(180, 640)  # Increase retry limit from 5 to 6
    click(180, 704)  # Save policy
    authenticate()
    qmp.capture(artifacts / "developer-service-saved.ppm")
    click(60, 228)  # Service list
    click(60, 228)  # Developer home
    click(180, 670)  # Core inventory
    qmp.capture(artifacts / "developer-core.ppm")

    click(240, 940)  # Home, then terminal through all apps
    click(240, 838)
    click(240, 426)
    guest.wait_lifecycle("org.leon.terminal")
    qmp.wait_frame(artifacts / "developer-terminal-ready.ppm",
                   lambda frame: frame[(180 * 480 + 24) * 3:(180 * 480 + 24) * 3 + 3]
                                 == bytes.fromhex("19151f"),
                   "Sandboxed terminal did not render")
    terminal_pid = guest.run("pidof leon-terminal")
    guest.run(f"grep -q 'Uid:[[:space:]]*11005' /proc/{terminal_pid}/status && "
              f"grep -q 'NoNewPrivs:[[:space:]]*1' /proc/{terminal_pid}/status")

    def command(text):
        qmp.type_text(text)
        qmp.key("ret")
        time.sleep(.8)

    def wait_proof(path, marker):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            proof = guest.run(f"cat {path} 2>/dev/null || true")
            if marker in proof:
                return proof
            time.sleep(.15)
        qmp.capture(artifacts / "developer-terminal-failed.ppm")
        raise RuntimeError("Root command proof missing: " + proof)

    command("su -c 'id -u > /run/leon-root/ui-root-proof; "
            "test -r /data/leon/rootd/policy.json && printf UNCONFINED >> /run/leon-root/ui-root-proof; "
            "sudo id >/dev/null 2>&1; test $? -eq 126 && printf SUDO_DISABLED >> /run/leon-root/ui-root-proof; "
            "cat /data/leon/developer/services/notifications.json >> /run/leon-root/ui-root-proof; "
            "chmod 644 /run/leon-root/ui-root-proof'")
    authenticate()
    proof = wait_proof("/run/leon-root/ui-root-proof", "SUDO_DISABLED")
    if not proof.startswith("0\nUNCONFINEDSUDO_DISABLED") or '"restart_limit":6' not in proof:
        raise RuntimeError("Root identity, isolation or UI service policy mismatch: " + proof)
    if guest.run("ls -ln /run/leon-root/ui-root-proof").split()[2] != "0":
        raise RuntimeError("Root session did not create a root-owned proof file")
    command("su -c 'printf SECOND_AUTH > /run/leon-root/ui-root-second; chmod 644 /run/leon-root/ui-root-second'")
    guest.run("test ! -e /run/leon-root/ui-root-second")  # Every session asks again.
    authenticate()
    wait_proof("/run/leon-root/ui-root-second", "SECOND_AUTH")
    qmp.capture(artifacts / "developer-terminal-su.ppm")
    (artifacts / "root-ui-proof.txt").write_text(proof + "\n")
    print("Target developer UI: private IME password, root switch, kernel/modules/core views, "
          "service restart/config and two authenticated sandboxed terminal su commands passed", flush=True)
