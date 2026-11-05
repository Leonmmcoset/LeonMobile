"""Real compositor input and captured sound for the platform services fixture."""
import array
import json
import math
import sys
import time
import wave


def verify_audio(path):
    with wave.open(str(path), "rb") as recording:
        if recording.getsampwidth() != 2:
            raise RuntimeError("Expected signed 16-bit QEMU audio capture")
        channels = recording.getnchannels()
        rate = recording.getframerate()
        pcm = array.array("h", recording.readframes(recording.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    mono = [sum(pcm[i:i + channels]) / channels for i in range(0, len(pcm), channels)]
    window = 4096
    def energy(start):
        return sum(v * v for v in mono[start:start + window])
    def magnitudes(start):
        data = mono[start:start + window]
        if len(data) != window or max(abs(v) for v in data) < 100:
            return [0, 0]
        result = []
        for frequency in (440, 660):
            real = sum(v * math.cos(2 * math.pi * frequency * i / rate) for i, v in enumerate(data))
            imag = sum(v * math.sin(2 * math.pi * frequency * i / rate) for i, v in enumerate(data))
            result.append(math.hypot(real, imag) / window)
        return result
    tones = max((magnitudes(start) for start in range(0, max(1, len(mono) - window), window)), key=min)
    if min(tones) < 20:
        raise RuntimeError("Captured output lacks both mixed test tones: " + repr(tones))
    print(f"QEMU WAV: {len(mono) / rate:.1f}s, {rate}Hz, both 440/660Hz tones verified {tones}", flush=True)


def exercise_platform(guest, qmp, artifacts):
    # Restart the actual kernel before using the desktop. The fixture validates
    # durable settings/DNS/default/audio policy and restores initial preferences.
    start = guest.log.stat().st_size
    guest.run("lmsys power '{\"op\":\"reboot\"}'")
    deadline = time.monotonic() + 100
    while time.monotonic() < deadline:
        with guest.log.open("rb") as stream:
            stream.seek(start)
            text = stream.read().decode(errors="replace").replace("\r", "")
        if "leond: started shell" in text and "\n$ " in text:
            break
        if "exited platform-test status=1" in text and "\n$ " in text:
            raise RuntimeError(guest.run("cat /run/leon/platform-tests.txt"))
        if guest.proc.poll() is not None:
            raise RuntimeError("QEMU exited during platform reboot")
        time.sleep(.2)
    else:
        raise RuntimeError("Platform reboot timed out")
    reboot = guest.run("cat /run/leon/platform-tests.txt")
    (artifacts / "platform-reboot.txt").write_text(reboot + "\n")
    if "PLATFORM_REBOOT_PASSED" not in reboot:
        raise RuntimeError(reboot)
    def system(service, request):
        import shlex
        return json.loads(guest.run("lmsys " + service + " " + shlex.quote(json.dumps(request))))

    def wait(predicate, message, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.2)
        qmp.capture(artifacts / "platform-ui-failed.ppm")
        raise RuntimeError(message)

    def click(x, y):
        qmp.click(x, y)
        time.sleep(.5)

    def foreground():
        state = json.loads(guest.run("lmpkg lifecycle"))["applications"]
        return {app for app, info in state.items() if info["foreground"]}

    def home():
        qmp.drag(240, 940, 240, 820)
        qmp.wait_frame(artifacts / "platform-home.ppm",
                       lambda frame: pixel(frame, 100, 240) == bytes.fromhex("6750a4"),
                       "Platform home did not commit")

    def drawer():
        click(240, 840)
        qmp.wait_frame(artifacts / "platform-apps.ppm",
                       lambda frame: pixel(frame, 80, 440) == bytes.fromhex("ffdf9e"),
                       "App drawer did not commit")

    def pixel(frame, x, y):
        return frame[(y * 480 + x) * 3:(y * 480 + x) * 3 + 3]
    wait(lambda: "Volume: 0.70" in guest.run("XDG_RUNTIME_DIR=/run/user/1000 wpctl get-volume @DEFAULT_AUDIO_SINK@"),
         "PipeWire output volume does not match the restored preference")
    qmp.wait_frame(artifacts / "platform-boot-lock.ppm",
                   lambda frame: pixel(frame, 240, 500) == bytes.fromhex("eaddff"),
                   "Platform lock screen did not commit")
    time.sleep(.5)
    click(240, 790)
    qmp.wait_frame(artifacts / "platform-unlocked.ppm",
                   lambda frame: pixel(frame, 100, 240) == bytes.fromhex("6750a4"),
                   "Platform swipe lock did not unlock")
    click(240, 526)  # Home -> LMUI demo
    wait(lambda: foreground() == {"org.leon.gallery"}, "Gallery did not open")
    qmp.wait_frame(artifacts / "platform-gallery-ready.ppm",
                   lambda frame: pixel(frame, 40, 695) == bytes.fromhex("eaddff"),
                   "Gallery did not commit its playback controls")
    click(240, 695)  # Real test-tone action
    wait(lambda: system("audio", {"op": "status"}).get("current", {}).get("app") == "org.leon.gallery", "Gallery playback failed")
    qmp.capture(artifacts / "platform-gallery-audio.ppm")
    click(240, 757)  # Share plain text
    wait(lambda: bool(system("intents", {"op": "pending"})["requests"]), "Sharing did not open chooser")
    qmp.wait_frame(artifacts / "platform-chooser.ppm",
                   lambda frame: pixel(frame, 50, 382) == bytes.fromhex("eaddff") and
                   pixel(frame, 30, 220) == bytes.fromhex("fffbff"),
                   "Interaction chooser did not commit")
    interaction_token = system("intents", {"op": "pending"})["requests"][0]["token"]
    click(398, 384)  # Always -> Editor
    wait(lambda: foreground() == {"org.leon.editor"}, "Chooser failed to focus editor")
    wait(lambda: system("intents", {"op": "result", "token": interaction_token}).get("acknowledged", False),
         "Editor did not receive the shared text")
    wait(lambda: "share:mime:text/plain" in system("intents", {"op": "defaults"})["defaults"], "Default choice was not saved")
    qmp.wait_frame(artifacts / "platform-editor-shared.ppm",
                   lambda frame: pixel(frame, 40, 208) == bytes.fromhex("eaddff") and
                   pixel(frame, 5, 500) == bytes.fromhex("fff8ff") and
                   pixel(frame, 5, 20) == bytes.fromhex("fff8ff"),
                   "Receiving editor did not commit")
    # The separate document picker asks for a file, not a whole directory grant.
    click(240, 208)
    wait(lambda: bool(system("documents", {"op": "pending"})["requests"]), "Document picker did not appear")
    qmp.wait_frame(artifacts / "platform-document-picker.ppm",
                   lambda frame: pixel(frame, 60, 718) == bytes.fromhex("eaddff") and
                   pixel(frame, 30, 240) == bytes.fromhex("fffbff"),
                   "Document picker did not commit")
    click(240, 718)  # Cancel (picker is centered at y=200)
    wait(lambda: not system("documents", {"op": "pending"})["requests"], "Document picker cancellation failed")
    qmp.wait_frame(artifacts / "platform-editor-returned.ppm",
                   lambda frame: pixel(frame, 5, 500) == bytes.fromhex("fff8ff") and
                   pixel(frame, 5, 20) == bytes.fromhex("fff8ff"),
                   "Document picker did not release input to the editor")
    # The player remains active after the sharing transition backgrounded Gallery.
    state = system("audio", {"op": "status"})
    if state.get("current", {}).get("app") != "org.leon.gallery":
        raise RuntimeError("Background playback ended during an app switch")
    qmp.drag(240, 20, 240, 300)
    qmp.drag(240, 110, 240, 450)
    qmp.wait_shade(artifacts / "platform-shade-media.ppm", 896)
    click(350, 292)  # Manual lock
    qmp.wait_frame(artifacts / "platform-lock-media.ppm",
                   lambda frame: pixel(frame, 30, 450) == bytes.fromhex("fffbff") and
                   pixel(frame, 240, 600) == bytes.fromhex("eaddff"),
                   "Media lock screen did not commit")
    click(397, 476)  # Lock-screen pause
    wait(lambda: system("audio", {"op": "status"})["current"]["paused"], "Lock-screen pause did not reach audiod")
    click(397, 476)
    wait(lambda: not system("audio", {"op": "status"})["current"]["paused"], "Lock-screen resume failed")
    click(240, 790)
    qmp.wait_frame(artifacts / "platform-media-unlocked.ppm",
                   lambda frame: pixel(frame, 100, 240) == bytes.fromhex("6750a4"),
                   "Media lock did not return home")
    drawer()
    click(96, 426)  # Settings, third row
    wait(lambda: foreground() == {"org.leon.settings"}, "Settings did not open")
    qmp.wait_frame(artifacts / "platform-settings-home.ppm",
                   lambda frame: pixel(frame, 28, 206) == bytes.fromhex("eaddff"),
                   "Settings home did not commit")
    click(180, 296)  # Network
    qmp.wait_frame(artifacts / "platform-settings-network.ppm",
                   lambda frame: pixel(frame, 240, 242) == bytes.fromhex("eaddff"),
                   "Network settings did not commit")
    # Enable/disable is a real Settings UID operation. Scroll to network controls.
    qmp.drag(410, 792, 410, 500)
    qmp.capture(artifacts / "platform-settings-network-controls.ppm")
    click(120, 486)
    wait(lambda: not system("network", {"op": "status"})["enabled"], "Settings failed to disable Ethernet")
    click(120, 486)
    wait(lambda: system("network", {"op": "status"})["connected"], "Settings failed to reconnect Ethernet")
    click(36, 75)
    qmp.wait_frame(artifacts / "platform-settings-home.ppm",
                   lambda frame: pixel(frame, 28, 206) == bytes.fromhex("eaddff"),
                   "Network settings did not return home")
    click(180, 464)  # Sound
    qmp.wait_frame(artifacts / "platform-settings-audio.ppm",
                   lambda frame: pixel(frame, 50, 763) == bytes.fromhex("eaddff"),
                   "Per-app audio controls did not commit")
    qmp.drag(410, 790, 410, 510)
    qmp.capture(artifacts / "platform-settings-audio-streams.ppm")
    click(68, 763)
    wait(lambda: system("audio", {"op": "status"})["current"]["volume"] == 90,
         "Settings failed to lower application volume")
    click(412, 763)
    wait(lambda: system("audio", {"op": "status"})["current"]["volume"] == 100,
         "Settings failed to restore application volume")
    click(36, 75)
    qmp.wait_frame(artifacts / "platform-settings-home.ppm",
                   lambda frame: pixel(frame, 28, 206) == bytes.fromhex("eaddff"),
                   "Sound settings did not return home")
    # Search avoids depending on the growing System page's scroll position.
    click(160, 206)
    qmp.type_text("default")
    click(180, 296)
    qmp.wait_frame(artifacts / "platform-settings-defaults.ppm",
                   lambda frame: pixel(frame, 30, 340) == bytes.fromhex("fffbff") and
                   pixel(frame, 240, 400) == bytes.fromhex("fffbff"),
                   "Saved defaults did not load in Settings")
    click(180, 340)
    wait(lambda: not system("intents", {"op": "defaults"})["defaults"], "Settings failed to clear default")
    # Leave Settings on Document access, then produce a real file interaction
    # from the sandboxed Files app and revoke its capabilities through the UI.
    click(36, 75)
    qmp.wait_frame(artifacts / "platform-settings-home.ppm",
                   lambda frame: pixel(frame, 28, 206) == bytes.fromhex("eaddff"),
                   "Default apps did not return home")
    click(160, 206)
    for _ in "default":
        qmp.key("backspace")
    qmp.type_text("document")
    click(180, 296)
    home()
    qmp.wait_frame(artifacts / "platform-files-home.ppm",
                   lambda frame: pixel(frame, 100, 240) == bytes.fromhex("6750a4"),
                   "Home did not return before Files")
    click(96, 526)
    wait(lambda: foreground() == {"org.leon.files"}, "Files did not open")
    qmp.wait_frame(artifacts / "platform-files-ready.ppm",
                   lambda frame: pixel(frame, 40, 244) == bytes.fromhex("eaddff"),
                   "Files did not commit")
    click(120, 244)
    qmp.wait_frame(artifacts / "platform-files-created.ppm",
                   lambda frame: pixel(frame, 40, 428) == bytes.fromhex("eaddff"),
                   "Files did not create its private document")
    # The broker's .tmp and fontconfig's .cache directories precede new.txt.
    click(180, 428)
    wait(lambda: bool(system("intents", {"op": "pending"})["requests"]), "Opening a file did not ask for a handler")
    qmp.wait_frame(artifacts / "platform-file-handler.ppm",
                   lambda frame: pixel(frame, 50, 382) == bytes.fromhex("eaddff") and
                   pixel(frame, 30, 220) == bytes.fromhex("fffbff"),
                   "File handler chooser did not commit")
    file_interaction = system("intents", {"op": "pending"})["requests"][0]["token"]
    click(180, 382)
    wait(lambda: system("intents", {"op": "result", "token": file_interaction}).get("acknowledged", False),
         "Editor did not consume the delegated document")
    wait(lambda: foreground() == {"org.leon.editor"}, "File handler did not enter the foreground")
    qmp.wait_frame(artifacts / "platform-file-opened.ppm",
                   lambda frame: pixel(frame, 40, 208) == bytes.fromhex("eaddff") and
                   pixel(frame, 5, 20) == bytes.fromhex("fff8ff"),
                   "Opened document did not commit")
    home()
    drawer()
    click(96, 426)
    wait(lambda: foreground() == {"org.leon.settings"}, "Settings did not resume")
    documents = system("documents", {"op": "list"})["documents"]
    source_index = next(i for i, doc in enumerate(documents) if doc["app"] == "org.leon.files")
    card_y = 338 + source_index * 148
    qmp.wait_frame(artifacts / "platform-settings-documents.ppm",
                   lambda frame: pixel(frame, 30, card_y) == bytes.fromhex("fffbff") and
                   pixel(frame, 240, card_y + 50) == bytes.fromhex("fffbff"),
                   "Document grants did not appear in Settings")
    # Preserve a settled evidence frame after the app/shell entrance motion.
    time.sleep(.5)
    qmp.capture(artifacts / "platform-settings-documents.ppm")
    click(180, card_y)
    wait(lambda: not system("documents", {"op": "list"})["documents"], "Settings failed to revoke document descendants")
    qmp.capture(artifacts / "platform-settings-documents-revoked.ppm")
    print("Platform UI: chooser/Always, file picker cancel, background audio, lock pause/resume, Settings network/volume/defaults, private file opening and grant revocation verified", flush=True)
