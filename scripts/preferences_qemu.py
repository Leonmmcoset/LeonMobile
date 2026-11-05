"""Real target Settings input, service identities, durable policy and captured chimes."""
import array
import datetime
import json
import math
import sys
import time
import wave

from PIL import Image


def verify_notification_audio(path, windows_path):
    with wave.open(str(path), "rb") as source:
        if source.getsampwidth() != 2:
            raise RuntimeError("Expected QEMU signed 16-bit audio")
        channels, rate = source.getnchannels(), source.getframerate()
        pcm = array.array("h", source.readframes(source.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    mono = [sum(pcm[i:i + channels]) / channels for i in range(0, len(pcm), channels)]
    results = []
    for window in json.loads(windows_path.read_text()):
        start = max(0, (window["start_byte"] - 44) // (channels * 2))
        end = max(start, (window["end_byte"] - 44) // (channels * 2))
        data = mono[start:end]
        peak = max((abs(value) for value in data), default=0)
        if window["audible"] and peak < 50:
            raise RuntimeError("Missing real notification sound: " + repr(window))
        if not window["audible"] and peak > 5:
            raise RuntimeError("Notification policy allowed sound: " + repr(window))
        tones = []
        if window["audible"]:
            size = 1024
            for frequency in (784, 1046):
                maximum = 0
                for start in range(0, max(1, len(data) - size), size // 2):
                    part = data[start:start + size]
                    real = sum(v * math.cos(2 * math.pi * frequency * i / rate) for i, v in enumerate(part))
                    imag = sum(v * math.sin(2 * math.pi * frequency * i / rate) for i, v in enumerate(part))
                    maximum = max(maximum, math.hypot(real, imag) / size)
                tones.append(maximum)
            if min(tones) < 5:
                raise RuntimeError("Missing two-note chime frequencies: " + repr(tones))
        results.append(dict(name=window["name"], peak=peak, tones=tones))
    windows_path.with_name("audio-proof.json").write_text(json.dumps(results, indent=2) + "\n")
    print("QEMU audio: notification/preview chimes and policy silence verified", flush=True)


def exercise_preferences(guest, qmp, artifacts, timeout):
    surface = bytes.fromhex("fffbff")
    container = bytes.fromhex("eaddff")
    primary = bytes.fromhex("6750a4")
    windows = []

    def fixture(op):
        output = guest.run("leon-test-preferences " + op, timeout=25)
        # Supervised recovery writes kernel/service status to the same console.
        for line in output.splitlines():
            if line.startswith("{"):
                try:
                    value = json.loads(line)
                    if isinstance(value, dict):
                        return value
                except json.JSONDecodeError:
                    pass
        raise RuntimeError("Missing fixture JSON response: " + output)

    def snapshot():
        return fixture("snapshot")

    def pixel(frame, x, y):
        return frame[(y * 480 + x) * 3:(y * 480 + x) * 3 + 3]

    def capture(name):
        path = artifacts / (name + ".ppm")
        frame = qmp.capture(path)
        with Image.open(path) as shot:
            shot.save(path.with_suffix(".png"))
        return frame

    def wait(predicate, message, limit=15):
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            value = snapshot()
            if predicate(value):
                return value
            time.sleep(.15)
        capture("preferences-failed")
        raise RuntimeError(message + ": " + json.dumps(value))

    def click(x, y):
        qmp.move(x, y); time.sleep(.1)
        qmp.button(True); time.sleep(.2)
        qmp.button(False); time.sleep(.6)

    def select_text(x, y, previous, value):
        click(x, y)
        for _ in previous:
            qmp.key("backspace")
        qmp.type_text(value)
        qmp.key("ret")
        time.sleep(.8)

    def settings_home():
        # Launch/focus through the real shell, retaining the trusted application UID.
        click(400, 22)
        qmp.wait_shade(artifacts / "preferences-shade.ppm", 700)
        click(372, 628)
        guest.wait_lifecycle("org.leon.settings")
        qmp.wait_frame(artifacts / "preferences-home.ppm",
                       lambda frame: pixel(frame, 80, 196) == container,
                       "Settings did not reach its home page")

    def search_page(query):
        click(160, 206)
        qmp.type_text(query)
        qmp.key("ret"); time.sleep(.7)
        click(180, 296)
        time.sleep(.7)

    def sound_window(name, audible, action):
        time.sleep(2.2)  # let the preceding stream and global alert throttle expire
        start = (artifacts / "preferences-audio.wav").stat().st_size
        action()
        time.sleep(2)
        end = (artifacts / "preferences-audio.wav").stat().st_size
        windows.append(dict(name=name, audible=audible, start_byte=start, end_byte=end))
        (artifacts / "audio-windows.json").write_text(json.dumps(windows, indent=2) + "\n")

    fixture("authorization")
    settings_home()
    search_page("date")
    qmp.wait_frame(artifacts / "datetime-loaded.ppm",
                   lambda frame: pixel(frame, 30, 240) == container,
                   "Date/time service did not load in Settings")
    capture("settings-date-time")
    click(400, 392)
    wait(lambda state: not state["time"]["automatic"], "Automatic time did not disable")
    click(400, 486)
    wait(lambda state: not state["settings"]["settings"]["clock_24h"], "12-hour format did not persist")
    click(200, 579)
    click(180, 236)
    qmp.type_text("asia/shang")
    qmp.key("ret"); time.sleep(.7)
    capture("settings-timezone-search")
    click(180, 310)
    wait(lambda state: state["time"]["timezone"] == "Asia/Shanghai", "Timezone search/selection failed")
    qmp.drag(460, 820, 460, 280)
    time.sleep(.8)  # reaches the page's deterministic maximum scroll
    manual = datetime.datetime.fromtimestamp(snapshot()["time"]["unix_seconds"],
                                            datetime.timezone(datetime.timedelta(hours=8))).strftime("%Y-%m-%d %H:%M:%S")
    select_text(180, 414, manual, "2031-02-30 07:08:09")
    click(180, 532)
    wait(lambda state: state["time"]["last_result"] != "manual", "Invalid date changed system time")
    capture("settings-invalid-date")
    select_text(180, 414, "2031-02-30 07:08:09", "2031-05-06 07:08:09")
    click(180, 532)
    state = wait(lambda state: state["time"]["last_result"] == "manual", "Manual date/time did not apply")
    expected = int(datetime.datetime(2031, 5, 5, 23, 8, 9, tzinfo=datetime.timezone.utc).timestamp())
    if not expected <= state["time"]["unix_seconds"] < expected + 30:
        raise RuntimeError("Manual input did not use the selected timezone")
    capture("settings-manual-time")
    sync = fixture("sync")
    if sync["last_result"] != "success" or not sync["last_sync"]:
        raise RuntimeError("Real SNTP result missing")
    qmp.drag(460, 280, 460, 820)
    time.sleep(1)
    capture("settings-synchronized-time")
    # Return to manual time so reboot does not contact the one-shot fixture server.
    click(400, 392)
    wait(lambda state: not state["time"]["automatic"], "Automatic time did not turn off after sync")
    click(36, 75)
    # Clear the home search through the displayed clear control.
    click(436, 206)
    search_page("notification")
    qmp.wait_frame(artifacts / "notifications-loaded.ppm",
                   lambda frame: pixel(frame, 22, 240) == surface,
                   "Notification service did not load in Settings")
    capture("settings-notifications")
    sound_window("notification", True, lambda: fixture("post"))
    sound_window("preview", True, lambda: click(180, 568))
    click(400, 342)
    wait(lambda state: state["policy"]["settings"]["dnd"], "Do not disturb did not enable")
    sound_window("do-not-disturb", False, lambda: fixture("post"))
    if len(snapshot()["queue"]["notifications"]) != 2:
        raise RuntimeError("Do not disturb discarded an enabled notification")
    click(400, 342)
    wait(lambda state: not state["policy"]["settings"]["dnd"], "Do not disturb did not disable")
    click(400, 248)
    wait(lambda state: not state["policy"]["settings"]["sound"], "Notification sound did not disable")
    sound_window("global-sound-off", False, lambda: fixture("post"))
    click(400, 248)
    wait(lambda state: state["policy"]["settings"]["sound"], "Notification sound did not re-enable")
    apps = snapshot()["policy"]["apps"]
    gallery = next(i for i, app in enumerate(apps) if app["id"] == "org.leon.gallery")
    # Repeated held touch drags reach the content boundary without kinetic overshoot.
    for _ in range(3):
        qmp.drag(460, 820, 460, 260)
    time.sleep(.8)
    capture("settings-notifications-apps")
    scroll = 680 + len(apps) * 152 - (960 - 42 - 64 - 16) + 16
    y = 660 + gallery * 152 + 42 - scroll
    click(400, y + 102)
    wait(lambda state: not next(a for a in state["policy"]["apps"] if a["id"] == "org.leon.gallery")["sound"],
         "Per-app sound did not disable")
    sound_window("app-sound-off", False, lambda: fixture("post"))
    click(400, y + 42)
    wait(lambda state: not next(a for a in state["policy"]["apps"] if a["id"] == "org.leon.gallery")["enabled"],
         "App notifications did not disable")
    if snapshot()["queue"]["notifications"] or not fixture("post").get("suppressed"):
        raise RuntimeError("Disabled app could retain or submit notifications")
    capture("settings-notifications-app-disabled")
    click(400, y + 42)
    wait(lambda state: next(a for a in state["policy"]["apps"] if a["id"] == "org.leon.gallery")["enabled"],
         "App notifications did not re-enable")
    fixture("post")
    for _ in range(3):
        qmp.drag(460, 260, 460, 820)
    time.sleep(.8)

    def lock_and_check(mode):
        click(400, 22)
        qmp.wait_shade(artifacts / "notifications-lock-compact.ppm", 700)
        click(400, 22)
        qmp.wait_shade(artifacts / "notifications-lock-expanded.ppm", 896)
        click(350, 292)
        state = wait(lambda state: state["auth"]["locked"] and state["desktop"]["privacy_locked"],
                     "Notification privacy did not follow authoritative lock")
        items = state["desktop"]["notifications"]
        if mode == "none":
            if items: raise RuntimeError("Hidden lock-screen notifications remained visible")
        elif not items or (mode == "hide" and (items[0]["title"] or not items[0].get("redacted"))):
            raise RuntimeError("Lock-screen content was not redacted")
        elif mode == "show" and items[0]["title"] != "Private LeonMobile notification":
            raise RuntimeError("Requested lock-screen content was not shown")
        time.sleep(1.5)
        capture("lock-notifications-" + mode)
        click(240, 790)
        qmp.wait_frame(artifacts / "preferences-unlocked.ppm",
                       lambda frame: pixel(frame, 100, 240) == primary,
                       "Unlock did not return home")
        click(400, 22)
        qmp.wait_shade(artifacts / "preferences-return-shade.ppm", 700)
        click(372, 628)
        guest.wait_lifecycle("org.leon.settings")
        time.sleep(1)

    lock_and_check("hide")
    click(90, 486)
    wait(lambda state: state["policy"]["settings"]["lockscreen"] == "show", "Show lock-screen content did not persist")
    lock_and_check("show")
    click(390, 486)
    wait(lambda state: state["policy"]["settings"]["lockscreen"] == "none", "Hide all lock notifications did not persist")
    lock_and_check("none")
    click(240, 486)
    wait(lambda state: state["policy"]["settings"]["lockscreen"] == "hide", "Redaction mode did not restore")
    before = snapshot()
    fixture("restart")
    after = snapshot()
    if before["policy"]["settings"] != after["policy"]["settings"] or after["queue"]["notifications"]:
        raise RuntimeError("Notification policy/restart queue semantics changed")
    if after["time"]["last_result"] != "disabled" or before["time"]["last_sync"] != after["time"]["last_sync"]:
        raise RuntimeError("Time synchronization history did not survive timed recovery")
    qmp.wait_frame(artifacts / "notification-service-ready.ppm",
                   lambda frame: pixel(frame, 22, 240) == surface,
                   "Notification Settings did not recover after daemon restart")
    capture("settings-notification-service-recovered")
    before_boot = guest.run("cat /proc/sys/kernel/random/boot_id")
    start = guest.log.stat().st_size
    guest.run("lmsys power '{\"op\":\"reboot\"}'")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = guest.log.read_text(errors="replace")[start:].replace("\r", "")
        if "leond: started shell" in text and "\n$ " in text: break
        if guest.proc.poll() is not None: raise RuntimeError("QEMU exited during preferences reboot")
        time.sleep(.2)
    else:
        raise RuntimeError("Preferences reboot timed out")
    if guest.run("cat /proc/sys/kernel/random/boot_id") == before_boot:
        raise RuntimeError("Kernel boot ID did not change")
    state = snapshot()
    if (state["policy"]["settings"] != before["policy"]["settings"] or
            state["time"]["timezone"] != "Asia/Shanghai" or state["time"]["automatic"] or
            state["settings"]["settings"]["clock_24h"] or not state["time"]["last_sync"]):
        raise RuntimeError("Settings did not survive a real reboot")
    time.sleep(2)
    capture("settings-reboot-lock-12h")
    click(240, 790)
    qmp.wait_frame(artifacts / "preferences-reboot-home.ppm",
                   lambda frame: pixel(frame, 100, 240) == primary,
                   "Reboot unlock did not reach home")
    settings_home()
    search_page("notification")
    qmp.wait_frame(artifacts / "notifications-reboot-ready.ppm",
                   lambda frame: pixel(frame, 22, 240) == surface,
                   "Persisted notification settings did not render after reboot")
    capture("settings-notifications-after-reboot")
    (artifacts / "preferences-proof.json").write_text(json.dumps(state, indent=2) + "\n")
    (artifacts / "audio-windows.json").write_text(json.dumps(windows, indent=2) + "\n")
    print("Target Settings: manual time, timezone search, 12h format, SNTP history, policy, privacy, recovery and reboot verified", flush=True)
