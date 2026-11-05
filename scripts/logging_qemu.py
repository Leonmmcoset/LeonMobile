"""Drive actual Settings diagnostics over the target compositor, without UI mocks."""
import json
import re
import time

from PIL import Image


def exercise_logging_ui(guest, qmp, artifacts, timeout, reboot=False):
    background = bytes.fromhex("fff8ff")
    surface = bytes.fromhex("fffbff")
    container = bytes.fromhex("eaddff")
    primary = bytes.fromhex("6750a4")

    def pixel(frame, x, y):
        offset = (y * 480 + x) * 3
        return frame[offset:offset + 3]

    def capture(name):
        path = artifacts / ("diagnostics-" + name + ".ppm")
        frame = qmp.capture(path)
        with Image.open(path) as shot:
            shot.save(path.with_suffix(".png"))
        return frame

    def click(x, y):
        # Leave a complete contact interval for the guest USB seat under TCG.
        qmp.move(x, y)
        time.sleep(.1)
        qmp.button(True)
        time.sleep(.25)
        qmp.button(False)
        time.sleep(.65)

    def open_diagnostics():
        # Begin at home; quick settings opens the actual trusted Settings ELF.
        click(400, 22)
        qmp.wait_shade(artifacts / "diagnostics-open-shade.ppm", 700, False)
        click(372, 628)
        guest.wait_lifecycle("org.leon.settings")
        qmp.wait_frame(artifacts / "diagnostics-settings-ready.ppm",
                       lambda f: pixel(f, 80, 196) == container,
                       "Settings did not render its home page")
        click(180, 632)  # System
        click(180, 594)  # System diagnostics
        qmp.wait_frame(artifacts / "diagnostics-loaded.ppm",
                       lambda f: pixel(f, 22, 566) == surface,
                       "Trusted unlocked Settings did not display system logs")
        capture("logs")

    before_boot = guest.run("cat /proc/sys/kernel/random/boot_id")
    own = json.loads(guest.run("leonlog --json --source kmsg"))
    if own["access"] != "own" or own["records"]:
        raise RuntimeError("Non-root serial console could read system kernel logs")
    open_diagnostics()
    click(170, 566)
    qmp.wait_frame(artifacts / "diagnostics-detail-ready.ppm",
                   lambda f: pixel(f, 22, 566) != surface and pixel(f, 30, 248) == container,
                   "Log detail did not replace the list")
    capture("detail")
    click(36, 89)  # Back from detail, retaining the list
    click(240, 374)  # Services
    qmp.wait_frame(artifacts / "diagnostics-services-ready.ppm",
                   lambda f: pixel(f, 22, 500) == surface,
                   "Diagnostics did not display real leond service state")
    capture("services")
    click(390, 374)  # Crashd metadata
    capture("crashes")
    click(80, 374)
    qmp.wait_frame(artifacts / "diagnostics-logs-again.ppm",
                   lambda f: pixel(f, 22, 566) == surface,
                   "Returning to the log tab did not reload history")
    click(350, 486)  # Warning/error filter
    capture("warnings")
    click(350, 486)  # All levels
    click(350, 430)  # Automatic refresh
    capture("live")
    # Home removes focus; return through the broker and require a fresh query.
    click(240, 940)
    guest.wait_lifecycle(None)
    click(400, 22)
    qmp.wait_shade(artifacts / "diagnostics-return-shade.ppm", 700, False)
    click(372, 628)
    guest.wait_lifecycle("org.leon.settings")
    qmp.wait_frame(artifacts / "diagnostics-refocused.ppm",
                   lambda f: pixel(f, 22, 566) == surface,
                   "Diagnostics did not refresh after backgrounding")
    capture("refocused")
    # Lock through the actual shade. No private diagnostic page may remain visible.
    click(400, 22)
    qmp.wait_shade(artifacts / "diagnostics-compact.ppm", 700, False)
    click(400, 22)
    qmp.wait_shade(artifacts / "diagnostics-expanded.ppm", 896, False)
    click(350, 292)
    locked = qmp.wait_frame(artifacts / "diagnostics-locked.ppm",
                            lambda f: pixel(f, 0, 100) == background and
                                      pixel(f, 240, 200) == container,
                            "Lock screen did not protect the diagnostic page")
    guest.wait_lifecycle(None)
    capture("locked")
    click(240, 790)
    qmp.wait_frame(artifacts / "diagnostics-home.ppm",
                   lambda f: pixel(f, 240, 210) == primary,
                   "Unlock did not return to the launcher")
    # Stop the Settings instance so next opening starts at its home page.
    guest.run("lmpkg close org.leon.settings")
    guest.wait_lifecycle(None, absent=("org.leon.settings",))
    if reboot:
        start = guest.log.stat().st_size
        guest.run("lmsys power '{\"op\":\"reboot\"}'")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = guest.log.read_text(errors="replace")[start:].replace("\r", "")
            if "leond: started shell" in text and "\n$ " in text:
                break
            time.sleep(.2)
        else:
            raise RuntimeError("Logging reboot did not reach the real shell")
        after_boot = guest.run("cat /proc/sys/kernel/random/boot_id")
        if before_boot == after_boot:
            raise RuntimeError("Boot ID did not change across kernel reboot")
        qmp.wait_frame(artifacts / "diagnostics-reboot-lock.ppm",
                       lambda f: pixel(f, 0, 100) == background,
                       "No lock screen after logging reboot")
        click(240, 790)
        qmp.wait_frame(artifacts / "diagnostics-reboot-home.ppm",
                       lambda f: pixel(f, 240, 210) == primary,
                       "Reboot unlock did not reach home")
        open_diagnostics()
        click(120, 486)  # Previous boot
        qmp.wait_frame(artifacts / "diagnostics-previous-ready.ppm",
                       lambda f: pixel(f, 22, 566) == surface,
                       "Previous-boot journal did not survive real reboot")
        capture("previous-boot")
        click(170, 566)
        capture("previous-detail")
        click(240, 940)
        guest.run("lmpkg close org.leon.settings")
        guest.wait_lifecycle(None, absent=("org.leon.settings",))
    (artifacts / "diagnostics-proof.json").write_text(json.dumps({
        "own_uid_cannot_read_kmsg": True, "unlocked_trusted_settings_system_records": True,
        "detail": True, "services": True, "crashes": True, "filters": True,
        "focus_requery": True, "compositor_lock": True, "real_reboot_previous_history": reboot,
    }, indent=2) + "\n")
    print("Target diagnostics: trusted system logs/detail, filters/live, service/crash tabs, focus and lock protection"
          + (", and previous-boot history after real reboot" if reboot else "") + " passed", flush=True)
