"""Actual lock/Settings input and fail-closed recovery on a disposable QEMU disk."""
import json
import time

PIN = '739182'
PASSWORD = 'device passphrase 2026!'
BACKGROUND = bytes.fromhex('fff8ff')
COVER = bytes((15, 12, 20))
PRIMARY = bytes.fromhex('6750a4')


def pixel(frame, x, y):
    return frame[(y * 480 + x) * 3:(y * 480 + x) * 3 + 3]


def exercise_authentication(guest, qmp, artifacts, timeout):
    def click(x, y):
        qmp.click(x, y)
        time.sleep(.4)

    def control(op):
        return guest.run('leon-test-auth --control ' + op, timeout=20)

    def state():
        return json.loads(control('status'))

    def wait_state(**expected):
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            actual = state()
            if all(actual.get(key) == value for key, value in expected.items()): return actual
            time.sleep(.15)
        qmp.capture(artifacts / 'auth-state-failed.ppm')
        raise RuntimeError('Authentication state did not settle: ' + str(actual))

    def home(label):
        return qmp.wait_frame(artifacts / (label + '.ppm'),
                              lambda f: pixel(f, 240, 210) == PRIMARY,
                              'Authenticated home did not appear: ' + label)

    def lock_page(label):
        return qmp.wait_frame(artifacts / (label + '.ppm'),
                              lambda f: pixel(f, 0, 100) == BACKGROUND and pixel(f, 240, 200) == bytes.fromhex('eaddff'),
                              'Opaque lock page did not recover: ' + label)

    def cover(label):
        return qmp.wait_frame(artifacts / (label + '.ppm'),
                              lambda f: all(pixel(f, x, y) == COVER for x, y in
                                             ((0, 100), (240, 200), (80, 400), (300, 800))),
                              'Compositor did not hide private content: ' + label)

    def enter(value):
        qmp.type_text(value)
        qmp.key('ret')
        time.sleep(.4)

    def unlock(secret=PIN, label='auth-unlocked'):
        click(240, 790)
        enter(secret)
        home(label)

    def security():
        click(400, 22)
        click(372, 628)
        guest.wait_lifecycle('org.leon.settings')
        # Return to the root Settings page even when its process survived a lock.
        for _ in range(3):
            click(36, 74)
        click(180, 632)
        click(180, 496)
        qmp.capture(artifacts / 'auth-security.ppm')

    home('auth-initial-home')
    security()
    click(180, 370)
    qmp.capture(artifacts / 'auth-setup-form.ppm')
    enter(PIN)
    qmp.capture(artifacts / 'auth-setup-confirm.ppm')
    enter(PIN)
    wait_state(kind='pin', configured=True, locked=False)
    qmp.capture(artifacts / 'auth-settings-pin.ppm')
    print('Target Settings: device PIN configured through private text input', flush=True)

    # Keep an app's implicit pointer grab while lock becomes authoritative.
    click(240, 940)
    click(96, 410)
    guest.wait_lifecycle('org.leon.calculator')
    qmp.move(200, 320)
    qmp.button(True)
    control('lock')
    lock_page('auth-private-app-hidden')
    qmp.button(False)
    guest.wait_lifecycle(None)
    control("bare-unlock")
    qmp.key('esc')
    qmp.chord(['alt'], 'f1')
    qmp.chord(['alt'], 'esc')
    wait_state(locked=True)
    lock_page('auth-shortcuts-still-locked')

    # Same UID and forged app_id must not become the compositor's shell or IME.
    for role in ('shell', 'ime'):
        guest.run('LEON_FORGED_ROLE=' + role + ' XDG_RUNTIME_DIR=/run/leon WAYLAND_DISPLAY=wayland-0 '
                  '/usr/bin/leon-test-auth-forged > /tmp/auth-forged-' + role + '.txt 2>&1 &')
        time.sleep(.7)
        lock_page('auth-forged-' + role + '-hidden')
    time.sleep(5)
    for role in ('shell', 'ime'):
        proof = guest.run('cat /tmp/auth-forged-' + role + '.txt')
        if 'FORGED_ROLE focused=0 keys=0' not in proof: raise RuntimeError('Forged role received focus/input: ' + proof)

    click(240, 790)
    qmp.type_text('73918')
    qmp.capture(artifacts / 'auth-pin-masked.ppm')
    # Complete a PIN through the actual touch keypad (2 is in its first row).
    click(240, 438)
    qmp.key('ret')
    wait_state(locked=False)
    home('auth-pin-keypad-unlocked')

    # A stopped shell cannot acknowledge a stale application buffer as a lock page.
    click(96, 410)
    guest.wait_lifecycle('org.leon.calculator')
    control('shell-stall')
    control('lock')
    cover('auth-stopped-shell-cover')
    control('shell-resume')
    lock_page('auth-shell-resumed-lock')
    old_shell = guest.run('pidof leon-shell')
    control('shell-crash')
    lock_page('auth-shell-crash-lock')
    if guest.run('pidof leon-shell') == old_shell: raise RuntimeError('Shell did not restart')
    unlock(label='auth-shell-crash-unlocked')

    click(96, 410)
    guest.wait_lifecycle('org.leon.calculator')
    control('stall')
    cover('auth-stopped-authd-cover')
    control('resume')
    lock_page('auth-resumed-authd-lock')
    unlock(label='auth-resumed-authd-unlocked')
    old_auth = guest.run('pidof authd')
    control('crash')
    lock_page('auth-crashed-authd-lock')
    if guest.run('pidof authd') == old_auth: raise RuntimeError('Authentication service did not restart')
    unlock(label='auth-crashed-authd-unlocked')
    print('Target compositor: private app hidden, stale frames and forged roles blocked; shell/authd stall and crash recovered locked', flush=True)

    control('idle')
    lock_page('auth-idle-lock')
    control('idle-reset')
    unlock(label='auth-idle-unlocked')
    control('lock')
    lock_page('auth-before-retry-limit')
    click(240, 790)
    for attempt in range(5):
        enter('000000')
        current = state()
        if not current['locked'] or not current['retry_after_ms']: raise RuntimeError('Wrong PIN did not apply retry delay')
        if attempt < 4: time.sleep(1.1)
    current = state()
    if current['retry_after_ms'] < 27000: raise RuntimeError('Repeated wrong PIN did not escalate the delay')
    control('crash')
    lock_page('auth-throttle-daemon-restart')
    if state()['retry_after_ms'] < 23000: raise RuntimeError('Daemon crash cleared authentication delay')

    # Restart the real kernel and filesystem, retaining the configured PIN and penalty.
    from root_qemu import reboot_and_probe
    reboot_and_probe(guest, artifacts, 2, timeout)
    report = guest.run('cat /run/leon/auth-tests.txt')
    (artifacts / 'auth-reboot-policy.txt').write_text(report + '\n')
    if '"kind":"pin"' not in report or '"configured":true' not in report:
        raise RuntimeError('Device PIN did not survive kernel reboot: ' + report)
    # rootd's independent reboot probe disables root; device authentication still works.
    guest.run("su --status | grep '\"enabled\":false'")
    lock_page('auth-reboot-pin-lock')
    click(240, 790)
    enter(PIN)
    time.sleep(.5)
    if pixel(qmp.capture(artifacts / 'auth-reboot-delay.ppm'), 240, 210) == PRIMARY:
        raise RuntimeError('Reboot bypassed the persisted credential delay')
    time.sleep(31)
    enter(PIN)
    home('auth-reboot-pin-unlocked')
    print('Target persistence: fifth failure delay survived authd crash and real kernel reboot; device PIN worked with root disabled', flush=True)

    security()
    click(180, 442)
    for value in (PIN, PASSWORD, PASSWORD): enter(value)
    time.sleep(3)
    guest.run("lmsys power '{\"op\":\"lock\"}'")
    lock_page('auth-password-lock')
    click(240, 790)
    qmp.wait_frame(artifacts / 'auth-password-ime.ppm',
                   lambda f: pixel(f, 3, 604) == bytes.fromhex('eaddff'),
                   'Lock password did not request the trusted private IME')
    click(137, 754)  # d while IME is in Chinese mode: password purpose must commit it literally.
    qmp.type_text(PASSWORD[1:])
    qmp.capture(artifacts / 'auth-password-masked.ppm')
    qmp.key('ret')
    home('auth-password-ime-unlocked')
    security()
    click(180, 514)
    enter(PASSWORD)
    time.sleep(3)
    guest.run("lmsys power '{\"op\":\"lock\"}'")
    lock_page('auth-disabled-lock')
    click(240, 790)
    home('auth-disabled-swipe-unlocked')
    print('Target credentials: PIN to password rotation, private IME unlock and authenticated removal passed', flush=True)
