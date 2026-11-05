"""Trusted Root application UI on the real QEMU compositor. Private disk only."""
import json
import time
from PIL import Image
PASSWORD = "RootDemo!2026"
APP = "org.example.rootdemo"


def exercise_root_applications(guest, qmp, artifacts, timeout, reboot=True):
    def pixel(frame, x, y):
        return frame[(y*480+x)*3:(y*480+x)*3+3]
    def wait(predicate, message, seconds=15):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if predicate(): return
            time.sleep(.15)
        capture("failure")
        raise RuntimeError(message)
    def click(x,y):
        qmp.click(x,y); time.sleep(.45)
    def capture(name):
        path=artifacts/("root-app-"+name+".ppm")
        qmp.capture(path)
        with Image.open(path) as frame: frame.save(path.with_suffix(".png"))
    def state():
        output=guest.run("su --status")
        return next(json.loads(line) for line in reversed(output.splitlines()) if line.startswith('{'))
    def home():
        qmp.drag(240,932,240,820); time.sleep(.8)
    def settings(first=True):
        home(); click(400,22)
        qmp.wait_shade(artifacts/"root-app-shade.ppm",700,False)
        click(372,628); guest.wait_lifecycle("org.leon.settings")
        if first:
            qmp.wait_frame(artifacts/"root-app-settings-ready.ppm",lambda f:pixel(f,80,196)==bytes.fromhex("eaddff"),"Settings home did not render before navigation")
        time.sleep(.5)
    idle_button = None
    def button_region(frame):
        return frame[448*480*3:492*480*3]
    def manager_ready(frame):
        if pixel(frame,30,416)!=bytes.fromhex("eaddff"):
            return False
        # The history label uses the primary color only after the asynchronous
        # authenticated operation has finished and its actions are enabled.
        # Small glyphs are antialiased and need not contain an exact #6750a4
        # pixel. Primary text is purple; muted busy text and its background
        # both have a blue-minus-red difference below 35.
        return sum(frame[(y*480+x)*3+2]-frame[(y*480+x)*3]>=35
                   for y in range(402,430) for x in range(140,340))>=50
    def launch():
        nonlocal idle_button
        home()
        click(96,408)
        guest.wait_lifecycle(APP)
        frame=qmp.wait_frame(artifacts/"root-app-demo-ready.ppm",lambda f:pixel(f,30,220)==bytes.fromhex("eaddff") and pixel(f,30,470)==bytes.fromhex("6750a4"),"Root demo did not render")
        time.sleep(.5)
        idle_button=button_region(qmp.capture(artifacts/"root-app-idle.ppm"))
    def request():
        # Settle the preceding focus transfer before holding the button contact.
        time.sleep(.8)
        qmp.move(240,470); time.sleep(.3); qmp.button(True); time.sleep(.35); qmp.button(False)
        time.sleep(.8)
        qmp.wait_frame(artifacts/"root-app-prompt-ready.ppm",lambda f:pixel(f,30,390)==bytes.fromhex("eaddff"),"Trusted Root confirmation did not replace the application")
    def credentials():
        click(240,748)
        qmp.wait_frame(artifacts/"root-app-private-ime.ppm",lambda f:pixel(f,3,604)==bytes.fromhex("eaddff"),"Root password did not acquire the private shared IME")
    def completed():
        qmp.wait_frame(artifacts/"root-app-result-ready.ppm",lambda f:pixel(f,30,220)==bytes.fromhex("eaddff") and pixel(f,30,390)!=bytes.fromhex("eaddff"),"Root demo did not regain focus after authorization")
        qmp.wait_frame(artifacts/"root-app-idle-result.ppm",lambda f:button_region(f)==idle_button,"Root SDK did not finish draining output and report completion")
        time.sleep(.2)
    guest.run("lmpkg install /usr/share/root-demo.lmpkg")
    uid=next(a["uid"] for a in json.loads(guest.run("lmpkg list")) if a["id"]==APP)
    if state()["enabled"]: raise RuntimeError("Root demo UI must start with Root disabled")
    settings()
    click(180,632); time.sleep(.5); click(180,412); time.sleep(.5)
    capture("developer-before-enable")
    click(400,257)
    qmp.wait_frame(artifacts/"root-app-enable-ime.ppm",lambda f:pixel(f,3,604)==bytes.fromhex("eaddff"),"Root setup did not acquire the private IME")
    qmp.type_text(PASSWORD); qmp.key("ret"); time.sleep(.3)
    qmp.type_text(PASSWORD); qmp.key("ret")
    wait(lambda:state().get("enabled"),"Settings did not enable Root with an independent password")
    capture("enabled")
    launch(); capture("application")
    request(); capture("command")
    # No navigation gesture or app key may bypass the system modal.
    qmp.drag(240,932,240,820); time.sleep(.3)
    qmp.wait_frame(artifacts/"root-app-modal-protected.ppm",lambda f:pixel(f,30,390)==bytes.fromhex("eaddff"),"A home gesture bypassed Root modal input protection")
    credentials(); qmp.type_text("wrong-password"); capture("password-masked"); qmp.key("ret")
    wait(lambda:state().get("retry_after_ms",0)>0,"Wrong Root password did not trigger the persistent cooldown")
    time.sleep(1.2)
    qmp.type_text(PASSWORD); qmp.key("ret")
    completed(); capture("root-result")
    running=json.loads(guest.run("lmpkg running")); pid=running[APP]
    status=guest.run(f"cat /proc/{pid}/status")
    if f"Uid:\t{uid}\t{uid}\t{uid}\t{uid}" not in status or "NoNewPrivs:\t1" not in status or "Seccomp:\t2" not in status:
        raise RuntimeError("Application lost its UID or sandbox after successful Root command")
    request(); capture("fresh-request")
    click(240,814); completed(); capture("blocked")
    click(240,474); time.sleep(.8); completed(); capture("blocked-retry")
    if reboot:
        start=guest.log.stat().st_size
        guest.run("lmsys power '{\"op\":\"reboot\"}'")
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            with guest.log.open('rb') as log:
                log.seek(start); text=log.read().decode(errors='replace').replace('\r','')
            if "leond: started shell" in text and "\n$ " in text: break
            time.sleep(.3)
        else: raise RuntimeError("Root app persistence reboot timed out")
        if not state()["enabled"]: raise RuntimeError("Root switch did not survive real reboot")
        # Boot regression fixture is one-shot per disposable disk, not rerun after reboot.
        qmp.wait_frame(artifacts/"root-app-reboot-ready.ppm",lambda f:pixel(f,0,100)==bytes.fromhex("fff8ff"),"Lock screen did not finish rendering after reboot")
        time.sleep(.6)
        capture("reboot-lock")
        qmp.click(240,790); time.sleep(1)
        launch(); click(240,474); time.sleep(.8); completed(); capture("reboot-blocked")
    settings(first=reboot)
    # Settings was restored at its developer home after the first part.
    if reboot:
        click(180,632); click(180,412)
    click(240,752)
    qmp.wait_frame(artifacts/"root-app-manager-ready.ppm",manager_ready,"Root app manager did not finish loading")
    capture("manager-blocked")
    click(130,590)
    qmp.type_text(PASSWORD); qmp.key("ret")
    qmp.wait_frame(artifacts/"root-app-unblocked-ready.ppm",manager_ready,"Authenticated unblocking did not finish and re-enable the manager")
    capture("manager-ask")
    click(240,416)
    qmp.wait_frame(artifacts/"root-app-history-ready.ppm",lambda f:pixel(f,30,416)!=bytes.fromhex("eaddff") and pixel(f,30,228)==bytes.fromhex("eaddff"),"Root authorization history did not render")
    capture("history")
    launch(); request(); credentials(); qmp.type_text(PASSWORD); qmp.key("ret"); completed()
    capture("final-result")
    persistence = " and actual reboot persistence" if reboot else ""
    print(f"Actual Root UI: masked IME, bad-password delay, fresh approval, modal protection, sandbox UID {uid}, block/unblock/history{persistence} passed",flush=True)
