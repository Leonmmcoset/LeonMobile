#!/usr/bin/env python3
"""Record a real-time portrait tour of the reference QEMU image as H.264 MP4."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time

from PIL import Image, ImageChops, ImageDraw, ImageGrab, ImageStat

REPO = Path(__file__).resolve().parents[1]


def load_controls():
    # Reuse the target check's bounded serial transport and mobile input mapping.
    spec = importlib.util.spec_from_file_location("leon_qemu_check", REPO / "scripts/test-qemu.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.Guest, module.QMP


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


class Tour:
    def __init__(self, qmp, guest, artifacts, started):
        self.qmp, self.guest = qmp, guest
        self.artifacts, self.started = artifacts, started
        self.chapters = []
        self.apps = []

    def mark(self, title):
        elapsed = time.monotonic() - self.started
        self.chapters.append({"seconds": round(elapsed, 3), "title": title})
        print(f"{elapsed:6.1f}s {title}", flush=True)

    def snapshot(self, name):
        path = self.artifacts / (name + ".ppm")
        self.qmp.capture(path)
        with Image.open(path) as frame:
            frame.save(path.with_suffix(".png"))
        path.unlink()

    def home(self):
        self.qmp.drag(240, 932, 240, 820)
        time.sleep(.8)

    def launch(self, name):
        app_id = "org.leon." + name
        index = next(i for i, app in enumerate(self.apps) if app["id"] == app_id)
        if index >= 6:
            self.qmp.click(240, 838)
            time.sleep(1)
            top = 190
        else:
            top = 408
        self.qmp.click(96 + index % 3 * 144, top + index // 3 * 118)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if app_id in json.loads(self.guest.run("lmpkg running")):
                time.sleep(3)
                return
            time.sleep(.2)
        raise RuntimeError("Application did not launch from its icon: " + app_id)

    def type_keys(self, text):
        codes = {" ": "spc", "-": "minus", ".": "dot", "/": "slash", "\n": "ret"}
        for character in text:
            self.qmp.key(codes.get(character, character))

    def type_touch(self, text):
        rows = ("qwertyuiop", "asdfghjkl", "zxcvbnm")
        for character in text:
            if character == " ":
                self.qmp.click(240, 842)
            else:
                row = next(i for i, keys in enumerate(rows) if character in keys)
                width = 460 / len(rows[row])
                self.qmp.click(10 + (rows[row].index(character) + .5) * width - 2,
                               683 + row * 52)
            time.sleep(.08)

    def run(self, env, temp):
        self.apps = json.loads(self.guest.run("lmpkg list"))
        self.guest.run("test -S /run/leon/notifications.sock")
        self.mark("锁屏与解锁")
        time.sleep(3)
        self.snapshot("01-lock")
        # Verify that the encoder captures the unscaled guest viewport, without
        # host decorations or cropping. This is separate from video encoding.
        with Image.open(self.artifacts / "01-lock.png") as frame:
            screen = ImageGrab.grab(xdisplay=env["DISPLAY"])
            if screen.size != (480, 960) or max(ImageStat.Stat(
                    ImageChops.difference(screen.convert("RGB"), frame.convert("RGB"))).mean) > 1:
                screen.save(self.artifacts / "unexpected-viewport.png")
                raise RuntimeError("X11 recording does not match the native 480x960 guest viewport")
        self.qmp.drag(240, 790, 240, 740)
        time.sleep(1)
        self.qmp.drag(240, 790, 240, 510)
        time.sleep(2)
        self.snapshot("02-home")

        self.mark("计算器：7 + 3 = 10")
        self.launch("calculator")
        for point in ((72, 384), (408, 556), (296, 556), (408, 642)):
            self.qmp.click(*point)
            time.sleep(.5)
        time.sleep(2)
        self.snapshot("03-calculator")
        self.home()

        self.mark("设置：主题、声音、存储与关于设备")
        self.launch("settings")
        self.snapshot("04-settings")
        self.qmp.click(180, 380)
        time.sleep(1.5)
        self.qmp.click(400, 257)
        self.guest.wait_setting("dark", True)
        time.sleep(2)
        self.snapshot("05-dark-theme")
        self.qmp.click(36, 75)
        time.sleep(1)
        self.qmp.click(180, 464)
        time.sleep(1.5)
        self.qmp.click(146, 346)
        self.guest.wait_setting("volume", 25)
        time.sleep(2)
        for position, title in ((548, "06-storage"), (716, "07-about")):
            self.qmp.click(36, 75)
            time.sleep(1)
            self.qmp.click(180, position)
            time.sleep(2)
            self.snapshot(title)
        self.home()

        self.mark("时钟与秒表")
        self.launch("clock")
        self.qmp.click(128, 542)
        time.sleep(4)
        self.qmp.click(128, 542)
        time.sleep(1)
        self.snapshot("08-stopwatch")
        self.qmp.click(352, 542)
        time.sleep(1)
        self.home()

        self.mark("文本编辑器与屏幕键盘")
        self.launch("editor")
        self.qmp.click(120, 250)
        time.sleep(1)
        self.type_touch("hello leonmobile")
        time.sleep(1)
        self.snapshot("09-editor-keyboard")
        self.qmp.click(30, 842)
        time.sleep(1)
        self.qmp.click(240, 844)
        time.sleep(2)
        self.snapshot("10-editor-saved")
        self.home()

        self.mark("文件管理器：新建文件")
        self.launch("files")
        self.qmp.click(120, 244)
        time.sleep(2)
        self.snapshot("11-files")
        self.home()

        self.mark("LMUI：组件、通知与下拉控制菜单")
        self.launch("gallery")
        self.qmp.click(420, 278)
        time.sleep(1)
        self.qmp.drag(140, 388, 360, 388)
        self.qmp.click(220, 210)
        time.sleep(2)
        self.qmp.drag(240, 20, 240, 300)
        time.sleep(2)
        self.snapshot("12-notification")
        self.qmp.drag(240, 110, 240, 450)
        time.sleep(2)
        self.snapshot("13-controls")
        self.qmp.click(350, 200)
        self.guest.wait_setting("dark", False)
        time.sleep(2)
        self.qmp.click(180, 540)
        time.sleep(2)
        self.home()

        self.mark("图片：打开 PNG 示例")
        picture = Image.new("RGB", (360, 360), "#fff8ff")
        draw = ImageDraw.Draw(picture)
        draw.ellipse((150, -80, 450, 220), fill="#eaddff")
        draw.rounded_rectangle((30, 80, 330, 280), radius=48, fill="#6750a4")
        draw.ellipse((124, 114, 236, 226), fill="#d0bcff")
        draw.ellipse((156, 146, 204, 194), fill="#fff8ff")
        asset = temp / "leon-demo.png"
        picture.save(asset)
        self.guest.upload(asset, "/tmp/leon-demo.png")
        self.guest.run("chmod 644 /tmp/leon-demo.png")
        self.launch("images")
        self.qmp.click(160, 150)
        time.sleep(.8)
        for _ in range(48):
            self.qmp.key("backspace")
        self.type_keys("/tmp/leon-demo.png")
        self.qmp.click(410, 150)
        time.sleep(3)
        self.snapshot("14-image")
        self.home()

        self.mark("终端：内核版本与 AArch64")
        self.launch("terminal")
        self.type_keys("uname -r\n")
        time.sleep(2)
        self.type_keys("uname -m\n")
        time.sleep(2)
        self.snapshot("15-terminal")

        self.mark("最近任务与应用状态保留")
        self.qmp.click(80, 940)
        time.sleep(2)
        self.snapshot("16-tasks")
        self.qmp.click(200, 190)
        time.sleep(2)
        self.snapshot("17-calculator-retained")
        self.home()

        self.mark("通过系统电源菜单关机")
        self.qmp.drag(240, 20, 240, 300)
        time.sleep(1.5)
        self.qmp.click(432, 628)
        time.sleep(2)
        self.snapshot("18-power-menu")
        self.qmp.click(240, 426)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path, default=REPO / "output/images")
    parser.add_argument("--output", type=Path, default=REPO / "build/recordings" /
                        ("LeonMobile-1.0.0-" + time.strftime("%Y%m%d-%H%M%S") + ".mp4"))
    args = parser.parse_args()
    output = args.output.resolve()
    if output.suffix.lower() != ".mp4" or output.exists():
        parser.error("--output must name a new .mp4 file")
    for binary in ("qemu-system-aarch64", "Xvfb", "xdotool", "ffmpeg", "ffprobe"):
        if not shutil.which(binary):
            parser.error("Missing host tool: " + binary)
    images = args.images.resolve()
    for name in ("Image", "leon-initramfs.cpio.gz", "leonmobile-qemu-aarch64.img"):
        if not (images / name).is_file():
            parser.error("Missing image: " + str(images / name))
    output.parent.mkdir(parents=True, exist_ok=True)
    artifacts = output.with_suffix("")
    artifacts.mkdir(exist_ok=False)
    Guest, QMP = load_controls()
    with tempfile.TemporaryDirectory(prefix="leon-record-") as directory:
        temp = Path(directory)
        disk = temp / "disk.img"
        subprocess.run(["cp", "--reflink=auto", "--sparse=always",
                        images / "leonmobile-qemu-aarch64.img", disk], check=True)
        xvfb = qemu = encoder = qmp = None
        serial = artifacts / "serial.log"
        try:
            with (artifacts / "xvfb.log").open("wb") as xlog, serial.open("wb") as log, \
                    (artifacts / "ffmpeg.log").open("wb") as encode_log:
                read_fd, write_fd = os.pipe()
                try:
                    xvfb = subprocess.Popen(
                        ["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "480x960x24",
                         "-nolisten", "tcp", "-noreset"], pass_fds=(write_fd,),
                        stdout=xlog, stderr=xlog)
                finally:
                    os.close(write_fd)
                with os.fdopen(read_fd) as ready:
                    if not select.select([ready], [], [], 10)[0]:
                        raise RuntimeError("Xvfb did not allocate a display")
                    display = ":" + ready.readline().strip()
                    if display == ":":
                        raise RuntimeError("Xvfb exited; inspect xvfb.log")
                env = dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11")
                endpoint = temp / "qmp.sock"
                qemu = subprocess.Popen([
                    "qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-smp", "4",
                    "-m", "2048", "-S", "-name", "LeonMobile-recording", "-kernel", str(images / "Image"),
                    "-initrd", str(images / "leon-initramfs.cpio.gz"), "-append",
                    "console=ttyAMA0 leon.root=/dev/vda1 rootwait ro", "-drive",
                    f"if=none,file={disk},format=raw,id=system", "-device", "virtio-blk-pci,drive=system",
                    "-device", "virtio-gpu-pci,xres=480,yres=960", "-device", "qemu-xhci",
                    "-device", "usb-kbd", "-device", "usb-tablet", "-netdev", "user,id=net0",
                    "-device", "virtio-net-pci,netdev=net0", "-audiodev", "none,id=audio",
                    "-device", "intel-hda", "-device", "hda-duplex,audiodev=audio", "-display", "sdl,gl=off",
                    "-monitor", "none", "-serial", "stdio", "-qmp", f"unix:{endpoint},server=on,wait=off"
                ], env=env, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 15
                window = None
                while time.monotonic() < deadline:
                    if qemu.poll() is not None:
                        raise RuntimeError("QEMU exited; inspect serial.log")
                    found = subprocess.run(["xdotool", "search", "--onlyvisible", "--pid", str(qemu.pid)],
                                           env=env, capture_output=True, text=True)
                    if found.returncode == 0 and endpoint.exists():
                        window = found.stdout.splitlines()[-1]
                        break
                    time.sleep(.1)
                if not window:
                    raise RuntimeError("QEMU display did not open")
                for command in (("windowsize", window, "480", "960"), ("windowmove", window, "0", "0")):
                    subprocess.run(["xdotool", *command], env=env, check=True)
                qmp = QMP(endpoint)
                raw = temp / "capture.mp4"
                started = time.monotonic()
                encoder = subprocess.Popen([
                    "ffmpeg", "-hide_banner", "-loglevel", "warning", "-y", "-f", "x11grab",
                    "-framerate", "30", "-video_size", "480x960", "-draw_mouse", "0", "-i", display + "+0,0",
                    "-an", "-c:v", "libx264", "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p",
                    "-threads", "2", "-r", "30", "-movflags", "+faststart", str(raw)
                ], stdin=subprocess.PIPE, stdout=encode_log, stderr=subprocess.STDOUT)
                tour = Tour(qmp, Guest(qemu, serial), artifacts, started)
                tour.mark("开机：QEMU AArch64 / Linux 7.3.1")
                time.sleep(1)
                if encoder.poll() is not None:
                    raise RuntimeError("FFmpeg exited; inspect ffmpeg.log")
                qmp.command("cont")
                deadline = time.monotonic() + 150
                while time.monotonic() < deadline:
                    text = serial.read_text(errors="replace").replace("\r", "")
                    if "leond: started shell" in text and "\n$ " in text:
                        try:
                            qmp.capture(temp / "ready.ppm")
                            break
                        except RuntimeError:
                            pass
                    if qemu.poll() is not None:
                        raise RuntimeError("QEMU exited before desktop startup")
                    time.sleep(.2)
                else:
                    raise RuntimeError("Desktop boot timed out")
                tour.run(env, temp)
                qemu.wait(timeout=30)
                if qemu.returncode:
                    raise RuntimeError("QEMU did not power off cleanly")
                time.sleep(1.5)
                encoder.stdin.write(b"q\n")
                encoder.stdin.flush()
                encoder.communicate(timeout=30)
                if encoder.returncode:
                    raise RuntimeError("FFmpeg did not finalize the recording")
                probe = json.loads(subprocess.check_output([
                    "ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(raw)
                ], text=True))
                duration = float(probe["format"]["duration"])
                metadata = temp / "chapters.txt"
                lines = [";FFMETADATA1", "title=LeonMobile 1.0.0 — boot and built-in apps",
                         "comment=Uncut real-time QEMU developer prototype capture; no audio"]
                for index, chapter in enumerate(tour.chapters):
                    end = tour.chapters[index + 1]["seconds"] if index + 1 < len(tour.chapters) else duration
                    lines += ["[CHAPTER]", "TIMEBASE=1/1000", f"START={int(chapter['seconds'] * 1000)}",
                              f"END={int(end * 1000)}", "title=" + chapter["title"]]
                metadata.write_text("\n".join(lines) + "\n")
                subprocess.run(["ffmpeg", "-v", "error", "-n", "-i", str(raw), "-i", str(metadata),
                                "-map_metadata", "1", "-map_chapters", "1", "-c", "copy",
                                "-movflags", "+faststart", str(output)], check=True)
                record = dict(video=output.name, duration_seconds=duration, fps=30, width=480, height=960,
                              real_time=True, audio=False, image_directory=str(images),
                              source_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                                                                    cwd=REPO, text=True).strip(),
                              chapters=tour.chapters)
                (artifacts / "recording.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n")
                print(f"Video: {output}\nDuration: {duration:.1f}s; 480x960, 30 fps, silent", flush=True)
        finally:
            stop(encoder)
            if qmp:
                qmp.stream.close()
                qmp.socket.close()
            stop(qemu)
            stop(xvfb)


if __name__ == "__main__":
    main()
