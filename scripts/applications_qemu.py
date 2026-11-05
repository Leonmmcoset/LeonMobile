"""Settings app management through real compositor input on a disposable disk."""
import json
import shlex
import time


def exercise_applications_ui(guest, qmp, artifacts, package, timeout, reboot=False):
    gallery, local = "org.leon.gallery", "org.example.hello"
    preferences = json.loads(guest.run("lmsys settings '{\"op\":\"status\"}'"))["settings"]
    container = bytes.fromhex("30293c" if preferences.get("dark") else "eaddff")
    background = bytes.fromhex("15121b" if preferences.get("dark") else "fff8ff")
    primary = bytes.fromhex("d0bcff" if preferences.get("dark") else "6750a4")
    def pixel(frame, x, y):
        return frame[(y * 480 + x) * 3:(y * 480 + x) * 3 + 3]

    def call(op, id=None):
        request = dict(op=op, **({"id": id} if id else {}))
        output = guest.run("leon-test-applications " + shlex.quote(json.dumps(request)))
        # Service startup/restart diagnostics share the serial console. The
        # observation helper emits a single complete JSON line.
        for line in reversed(output.splitlines()):
            try:
                value = json.loads(line)
                if isinstance(value, dict): return value
            except ValueError:
                pass
        raise RuntimeError("Management probe returned no JSON: " + output)

    def info(id):
        response = call("app_info", id)
        if not response.get("ok"): raise RuntimeError(str(response))
        return response["app"]

    def wait_for(check, description):
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            if check(): return
            time.sleep(.15)
        qmp.capture(artifacts / "applications-failed.ppm")
        raise RuntimeError(description)

    def click(x, y):
        qmp.click(x, y)
        time.sleep(.45)

    def capture(name):
        qmp.capture(artifacts / ("applications-" + name + ".ppm"))

    def back():
        click(36, 75)

    def filter_apps(value, previous=""):
        click(140, 230)
        qmp.wait_frame(artifacts / "applications-keyboard.ppm",
                       lambda frame: pixel(frame, 3, 604) == container,
                       "App filter did not acquire the text input/IME")
        for _ in previous: qmp.key("backspace")
        qmp.type_text(value)
        qmp.key("esc")
        time.sleep(.6)

    def select(id, previous=""):
        filter_apps(id, previous)
        click(180, 366)
        qmp.wait_frame(artifacts / "applications-info-ready.ppm",
                       lambda frame: pixel(frame, 30, 310) == container,
                       "Application information did not render")
        time.sleep(.3)

    # Produce real application-owned state and exercise stop while it runs.
    destination = "/data/home/leon/management-hello.lmpkg"
    guest.upload(package, destination)
    guest.run("lmpkg install " + destination)
    guest.run("lmpkg launch " + local)
    time.sleep(.5)
    guest.run("lmpkg close " + local)
    wait_for(lambda: not info(local)["running"], "SDK app did not finish closing")
    guest.run("lmpkg launch " + gallery)
    wait_for(lambda: info(gallery)["running"], "Gallery did not launch")
    click(240, 940)
    qmp.wait_frame(artifacts / "applications-home-ready.ppm",
                   lambda frame: pixel(frame, 240, 210) == primary,
                   "Home did not settle before opening quick settings")
    click(400, 22)
    qmp.wait_shade(artifacts / "applications-shade-ready.ppm", 700, preferences.get("dark"))
    click(372, 628)
    guest.wait_lifecycle("org.leon.settings")
    for _ in range(3): back()
    qmp.wait_frame(artifacts / "applications-settings-home.ppm",
                   lambda frame: pixel(frame, 80, 196) == container,
                   "Settings home did not finish rendering")
    click(180, 800)  # Direct Apps category after About, preserving old rows.
    qmp.wait_frame(artifacts / "applications-list-ready.ppm",
                   lambda frame: pixel(frame, 130, 240) == container,
                   "Apps category did not render its filter")
    capture("list")

    select(gallery)
    current = info(gallery)
    if current["source"] != "system-image" or current["actions"]["uninstall"]:
        raise RuntimeError("Builtin management metadata is incorrect")
    if not current["storage"]["package"]["available"] or current["storage"]["package"]["bytes"] <= 0:
        raise RuntimeError("Builtin executable usage is missing")
    capture("builtin-info")
    click(90, 458)
    wait_for(lambda: not info(gallery)["running"], "Settings Stop did not stop Gallery")
    if info(gallery)["storage"]["data"]["bytes"] <= 0:
        raise RuntimeError("Stop unexpectedly removed private data")
    click(240, 458)
    capture("clear-confirm")
    click(120, 554)
    if info(gallery)["storage"]["data"]["bytes"] <= 0:
        raise RuntimeError("Cancel clear removed private data")
    click(240, 458)
    click(360, 554)
    wait_for(lambda: info(gallery)["storage"]["data"] == dict(available=True, bytes=0),
             "Confirmed clear did not empty private storage")
    if info(gallery)["storage"]["runtime"] != dict(available=True, bytes=0):
        raise RuntimeError("Clear retained app runtime files")
    capture("cleared")

    back()
    select("org.leon.settings", gallery)
    if any(info("org.leon.settings")["actions"].values()):
        raise RuntimeError("Settings must protect its own stop/clear/uninstall")
    click(90, 458); click(240, 458); click(390, 458)
    if not info("org.leon.settings")["running"]:
        raise RuntimeError("Disabled Settings self actions stopped Settings")
    capture("self-protected")

    back()
    select("org.leon.editor", "org.leon.settings")
    if not any(p["name"] == "files.import" and p["state"] != "unavailable"
               for p in info("org.leon.editor")["permissions"]):
        raise RuntimeError("Declared permission status is missing")
    qmp.drag(420, 810, 420, 500)
    capture("permissions")
    back()

    select(local, "org.leon.editor")
    current = info(local)
    if current["version"] != "1.0.0" or current["source"] != "local-unsigned":
        raise RuntimeError("Local package version/source is incorrect")
    if not current["actions"]["uninstall"] or current["storage"]["package"]["bytes"] <= 0:
        raise RuntimeError("Local package usage/uninstall action is missing")
    capture("local-info")
    click(390, 458)
    capture("uninstall-confirm")
    click(120, 554)
    if not call("app_info", local).get("ok"):
        raise RuntimeError("Cancel uninstall removed the package")
    click(390, 458)
    click(360, 554)
    wait_for(lambda: not call("app_info", local).get("ok"), "Settings did not uninstall SDK app")
    if any(app["id"] == local for app in call("app_list")["apps"]):
        raise RuntimeError("Uninstalled app remains in catalog")
    capture("uninstalled")

    if reboot:
        start = guest.log.stat().st_size
        guest.run("lmsys power '{\"op\":\"reboot\"}'")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with guest.log.open("rb") as stream:
                stream.seek(start)
                text = stream.read().decode(errors="replace").replace("\r", "")
            if ("LeonMobile 1.0.0: init ready" in text and
                    "leond: started shell" in text and "\n$ " in text): break
            if guest.proc.poll() is not None: raise RuntimeError("QEMU exited during reboot")
            time.sleep(.2)
        else: raise RuntimeError("Application management reboot timed out")
        if guest.run("id -u") != "1000": raise RuntimeError("Console identity changed on reboot")
        if call("app_info", local).get("ok") or info(gallery)["storage"]["data"]["bytes"]:
            raise RuntimeError("Uninstall/data reset did not survive actual reboot")
        qmp.wait_frame(artifacts / "applications-reboot-lock.ppm",
                       lambda frame: pixel(frame, 0, 100) == background and
                                     pixel(frame, 240, 200) == container,
                       "Lock screen did not finish rendering after reboot")
        click(240, 790)
        qmp.wait_frame(artifacts / "applications-rebooted.ppm",
                       lambda frame: pixel(frame, 240, 210) == primary,
                       "Home did not finish rendering after reboot unlock")
        # A fresh sandboxed launch remains possible after the persisted reset.
        guest.run("lmpkg launch " + gallery)
        wait_for(lambda: info(gallery)["running"], "Cleared builtin failed to relaunch after reboot")
        guest.run("lmpkg close " + gallery)
        wait_for(lambda: not info(gallery)["running"], "Relaunched builtin failed to close")

    (artifacts / "applications-proof.json").write_text(json.dumps(
        dict(gallery=info(gallery), installed=call("app_list")["apps"], reboot=reboot),
        ensure_ascii=False, indent=2) + "\n")
    print("Target Settings apps: version/source/permission/storage, protected actions, stop, "
          "cancel/confirm clear and uninstall" + (", reboot and relaunch" if reboot else "") +
          " passed", flush=True)
