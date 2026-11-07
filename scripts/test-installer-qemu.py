#!/usr/bin/env python3
"""Drive Files, the real installer and system picker on a disposable EFI disk."""
import argparse
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import time

from PIL import Image
from leon_image import copy_region, partitions

REPO = Path(__file__).resolve().parents[1]
ARTIFACTS = REPO / "build/qemu-installer-test"
spec = importlib.util.spec_from_file_location("disk_test", REPO / "scripts/test-disk-boot-qemu.py")
disk_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(disk_test)
disk_test.ARTIFACTS = ARTIFACTS


def inject(images, disk, temp, probe, payload):
    manifest = json.loads((REPO / "examples/hello/manifest.json").read_text())
    files = {}
    for name, version, permissions in (("01-install.lmpkg", "1.0.0", []),
        ("02-update.LMPKG", "1.1.0", ["network"]),
        ("04-picker.lmpkg", "1.2.0", ["network", "files.import"])):
        config = temp / (version + ".json")
        config.write_text(json.dumps({**manifest, "version": version, "permissions": permissions}))
        files[name] = temp / name
        subprocess.run(["python3", REPO / "scripts/make-package.py", config, payload, files[name]], check=True)
    files["03-invalid.lmpkg"] = temp / "03-invalid.lmpkg"
    files["03-invalid.lmpkg"].write_bytes(b"damaged package\n")
    rootfs = temp / "rootfs.ext4"
    subprocess.run(["cp", "--reflink=auto", "--sparse=always", images / "rootfs.ext4", rootfs], check=True)
    commands = ["mkdir /usr/share/installer-test",
                "write " + json.dumps(str(probe)) + " /usr/bin/leon-test-installer",
                "set_inode_field /usr/bin/leon-test-installer mode 0104755"]
    for name, source in files.items():
        commands.append("write " + json.dumps(str(source)) + " /usr/share/installer-test/" + name)
    batch = temp / "debugfs.commands"
    batch.write_text("\n".join(commands) + "\n")
    result = subprocess.run(["debugfs", "-w", "-f", batch, rootfs], capture_output=True, text=True, check=True)
    if any(error in result.stderr for error in ("not found", "already exists", "Could not")):
        raise RuntimeError(result.stderr)
    with disk.open("r+b") as target, rootfs.open("rb") as source:
        offset, size = partitions(target)["system"]
        if rootfs.stat().st_size != size: raise RuntimeError("system image size mismatch")
        target.seek(offset)
        copy_region(source, target, size)


def pixel(frame, x, y):
    offset = (y * 480 + x) * 3
    return frame[offset:offset + 3]


def exercise(proc, qmp, guest, log):
    disk_test.healthy(proc, guest, log)
    print("PASS ARM64 EFI boot and system services", flush=True)
    guest.run("leon-test-installer seed")
    guest.run("leon-test-installer denials")

    def status():
        return json.loads(guest.run("leon-test-installer status"))

    def wait_state(state, timeout=45, after=None):
        deadline = time.monotonic() + timeout
        result = {}
        while time.monotonic() < deadline:
            result = status()
            if after is not None and result.get("token") == after:
                time.sleep(.2)
                continue
            if result.get("state") == state: return result
            if result.get("state") == "failed" and state != "failed": break
            time.sleep(.2)
        capture("failure")
        raise RuntimeError("Installer did not reach " + state + ": " + repr(result))

    def capture(name):
        ppm = ARTIFACTS / (name + ".ppm")
        qmp.capture(ppm)
        Image.open(ppm).save(ppm.with_suffix(".png"))

    def apps():
        return json.loads(guest.run("lmpkg list"))

    def version():
        return next((a["version"] for a in apps() if a["id"] == "org.example.hello"), None)

    def open_app(id):
        qmp.click(240, 940)
        qmp.wait_frame(ARTIFACTS / "home-ready.ppm",
            lambda f: pixel(f, 400, 300) == bytes.fromhex("6750a4"), "Home did not settle")
        qmp.click(240, 838)
        qmp.wait_frame(ARTIFACTS / "apps-ready.ppm",
            lambda f: pixel(f, 30, 300) == bytes.fromhex("fff8ff"), "App list did not settle")
        catalogue = apps()
        index = next(i for i, app in enumerate(catalogue) if app["id"] == id)
        qmp.click(96 + (index % 3) * 144, 190 + (index // 3) * 118)
        result = guest.wait_lifecycle(id)
        guest.wait_lifecycle(id, pids={id: result["applications"][id]["pid"]})
        time.sleep(.5)

    def open_file(name):
        open_app("org.leon.files")
        qmp.wait_frame(ARTIFACTS / "files-ready.ppm",
            lambda f: pixel(f, 30, 190) == bytes.fromhex("eaddff"), "Files did not render its controls", 20)
        listing = guest.run("leon-test-installer files").splitlines()
        index = listing.index(name)
        qmp.move(180, 308 + index * 60)
        time.sleep(.05)
        qmp.button(True)
        time.sleep(.2)
        capture("file-pressed")
        qmp.button(False)
        try:
            guest.wait_lifecycle("org.leon.installer")
        except RuntimeError:
            print("Package interaction diagnostics: " + guest.run("lmsys intents '{\"op\":\"pending\"}'"), flush=True)
            print("Package document diagnostics: " + guest.run("lmsys documents '{\"op\":\"list\"}'"), flush=True)
            print("Package service diagnostics: " + guest.run("leon-test-installer diagnose"), flush=True)
            raise

    def ready(name, expected, after=None):
        result = wait_state("ready", after=after)
        if result["review"]["package"]["version"] != expected:
            raise RuntimeError("Wrong package review: " + repr(result))
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            documents = json.loads(guest.run("lmsys documents '{\"op\":\"list\"}'"))["documents"]
            if not any(d["app"] in ("org.leon.files", "org.leon.installer") for d in documents): break
            time.sleep(.2)
        else: raise RuntimeError("Package opening leaked its document capabilities")
        qmp.wait_frame(ARTIFACTS / "review-ready.ppm",
            lambda f: pixel(f, 430, 832) == bytes.fromhex("6750a4"), "Review controls did not render", 20)
        capture(name)
        (ARTIFACTS / (name + ".json")).write_text(json.dumps(result, indent=2, ensure_ascii=False))
        print("PASS " + name + " " + expected, flush=True)
        return result

    def confirm(name, expected):
        qmp.click(350, 836)
        wait_state("installed")
        if version() != expected: raise RuntimeError("Confirmed version was not installed")
        time.sleep(.8)
        capture(name)
        qmp.click(240, 836)
        wait_state("idle")
        print("PASS " + name + " " + expected, flush=True)

    qmp.click(240, 790)
    qmp.wait_frame(ARTIFACTS / "unlocked.ppm",
        lambda f: pixel(f, 400, 300) == bytes.fromhex("6750a4"), "Initial desktop did not unlock")
    open_file("01-install.lmpkg")
    ready("install-review", "1.0.0")
    if version() is not None: raise RuntimeError("Opening a package silently installed it")
    qmp.click(120, 836)
    wait_state("idle")
    if version() is not None: raise RuntimeError("Cancel installed an application")
    open_file("01-install.lmpkg")
    ready("expired-review", "1.0.0")
    guest.run("leon-test-installer discard")
    qmp.click(350, 836)
    qmp.wait_frame(ARTIFACTS / "expired-review-recovered.ppm",
        lambda f: pixel(f, 430, 560) == bytes.fromhex("6750a4"), "Expired review did not recover to package selection", 20)
    capture("expired-review-recovered")
    if version() is not None: raise RuntimeError("Expired review installed an application")
    open_file("01-install.lmpkg")
    ready("install-confirmation", "1.0.0")
    confirm("installed", "1.0.0")
    original_uid = next(a["uid"] for a in apps() if a["id"] == "org.example.hello")
    open_app("org.example.hello")
    qmp.wait_frame(ARTIFACTS / "installed-app.ppm",
        lambda f: pixel(f, 40, 286) == bytes.fromhex("6750a4"), "Installed app did not render", 20)
    capture("installed-app")
    guest.run("lmpkg close org.example.hello")
    time.sleep(.5)

    open_file("02-update.LMPKG")
    reviewed = ready("update-review", "1.1.0")
    if reviewed["review"]["installed"]["version"] != "1.0.0" or \
            reviewed["review"]["added_permissions"] != ["network"]:
        raise RuntimeError("Update review lost old version or added permissions")
    confirm("updated", "1.1.0")
    permission = json.loads(guest.run("leon-test-installer permissions"))
    if not permission.get("ok") or any(p["state"] == "allowed" for p in permission["permissions"]):
        raise RuntimeError("Installation silently granted a runtime permission")

    open_file("03-invalid.lmpkg")
    failed = wait_state("failed")
    qmp.wait_frame(ARTIFACTS / "invalid-ready.ppm",
        lambda f: pixel(f, 430, 560) == bytes.fromhex("6750a4"), "Invalid-package error did not render", 20)
    capture("invalid-package")
    if version() != "1.1.0": raise RuntimeError("Invalid package changed installed application")

    # The independent picker supports importing a package outside Files' home.
    qmp.click(240, 560)  # Installer's Choose package (client origin is y=42).
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        requests = json.loads(guest.run("lmsys documents '{\"op\":\"pending\"}'"))["requests"]
        if requests: break
        time.sleep(.2)
    else: raise RuntimeError("Installer did not open the system document picker")
    time.sleep(.7)
    capture("system-picker")
    qmp.click(180, 376)  # First shared file row in the trusted picker.
    ready("picker-update-review", "1.2.0", after=failed["token"])
    confirm("picker-update-installed", "1.2.0")
    if next(a["uid"] for a in apps() if a["id"] == "org.example.hello") != original_uid:
        raise RuntimeError("Upgrade changed the application UID")

    old_id = guest.run("cat /proc/sys/kernel/random/boot_id")
    offset = log.stat().st_size
    guest.run("lmsys power '{\"op\":\"reboot\"}'")
    disk_test.healthy(proc, guest, log, offset)
    if guest.run("cat /proc/sys/kernel/random/boot_id") == old_id or version() != "1.2.0":
        raise RuntimeError("Installed update did not survive a real reboot")
    if status()["state"] != "idle": raise RuntimeError("Reboot restored a stale installation transaction")
    qmp.click(240, 790)
    open_app("org.example.hello")
    qmp.wait_frame(ARTIFACTS / "updated-app.ppm",
        lambda f: pixel(f, 40, 286) == bytes.fromhex("6750a4"), "Updated app did not render after reboot", 20)
    capture("updated-app-after-reboot")
    disk_test.stop_desktop(proc, guest)
    print("PASS Files package opening, explicit cancel/install, expired review recovery, capability cleanup, installed app launch, uppercase extension, "
          "upgrade permission review, invalid archive, system picker, UID retention and real reboot", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path, default=REPO / "output/images")
    args = parser.parse_args()
    images = args.images.resolve()
    probe = REPO / "build/installer-probe"
    payload = REPO / "build/hello/payload"
    if not probe.is_file() or not (payload / "bin/hello").is_file():
        parser.error("run scripts/build-target-probes.sh and scripts/build-example.sh first")
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="leon-installer-qemu-") as name:
        temp = Path(name)
        disk = temp / "installer.img"
        disk_test.clone(images, disk)
        inject(images, disk, temp, probe, payload)
        with disk_test.session(images, disk, "installer") as (proc, qmp, guest, log):
            exercise(proc, qmp, guest, log)
        # Inspect the stopped ext4 independently, without granting the console
        # access to the application's private files.
        with disk.open("rb") as source, (temp / "data.ext4").open("wb") as target:
            offset, size = partitions(source)["data"]
            source.seek(offset); copy_region(source, target, size)
        witness = subprocess.check_output(["debugfs", "-R",
            "cat /leon/data/org.example.hello/installer-witness", temp / "data.ext4"],
            stderr=subprocess.DEVNULL, text=True)
        if witness != "graphical upgrade preserves private data\n":
            raise RuntimeError("Upgrade lost application-private user data")
        (ARTIFACTS / "preserved-data.txt").write_text(witness)
    print("Graphical installer QEMU checks passed; disposable disk removed. Evidence: " + str(ARTIFACTS), flush=True)


if __name__ == "__main__":
    main()
