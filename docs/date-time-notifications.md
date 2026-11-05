# Date, time and notification settings

Settings → System → Date & time controls timed. Settings search also finds it
using `date`, `time`, `日期` or `时区`. The page provides automatic network time,
an immediate synchronization action, a searchable installed IANA timezone list,
12/24-hour format, manual local date/time and the latest synchronization result.
Clock format follows settingsd and applies to the status bar, lock screen,
notification shade and Clock app.

Disable automatic time before applying a manual value. Input uses
`YYYY-MM-DD HH:MM:SS` in the selected timezone. Invalid calendar dates and
ambiguous/nonexistent DST times are rejected. The supported Unix time interval
is 2020–2100. The SNTP server field accepts numeric IPv4 addresses. Clock/RTC,
storage and network failures are reported; successful clock changes remain
successful even if a separate RTC write fails. The latest successful network
timestamp and result history survive daemon and kernel restarts. A scheduled
request is not reported as a completed synchronization.

Settings → Notifications controls notificationd. This category is scrollable
on the home page and searchable using `notification`, `通知` or `勿扰`. It offers:

* Global notification sound and do-not-disturb switches, and a sound preview.
* Per-installed-app notification and sound switches.
* Lock-screen content modes: show content, hide content, or show no notifications.

Turning an app's notifications off removes its queued messages and suppresses
later submissions. Turning its sound off keeps messages and silences alerts.
Do not disturb preserves notifications while silencing all notification alerts;
the preview honors the same global policy. Audio uses audiod/PipeWire and the
master volume/mute policy, with a two-second global automatic-alert rate limit.
The current built-in chime is fixed; choosing custom ringtones and scheduled
do-not-disturb periods are not implemented.

Content is hidden by default on the lock screen. Notificationd consults authd
and removes titles before sending the desktop's locked-session response. The
shell clears its previous notification cache when locking and checks the lock
epoch before drawing up to two passive notification cards. Authd outages hide
content. Settings may modify policy but cannot read the notification queue;
ordinary apps cannot modify system policy or read another app's notifications.

Preferences are durable. Notification messages themselves remain a bounded,
volatile queue and are cleared on notificationd restart. Delivery does not
depend on the availability of audio playback or the desktop. These pages use
asynchronous requests; a tap during a background read is queued for execution,
while duplicate mutations are disabled until the current mutation completes.

Service protocols are specified in [interfaces.md](interfaces.md) and
[core-services.md](core-services.md). This remains the QEMU developer prototype;
these controls do not establish physical-phone or network-time security support.

## Target validation

```sh
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 240 --preferences-probe build/preferences-probe
```

The test drives the production Settings ELF through the target Wayland compositor,
checks real service peer credentials, applies a manual date in the selected
timezone, performs local UDP SNTP synchronization, and exercises notification
switches and all lock-screen modes. It kills timed/notificationd and performs a
real kernel reboot to check configuration and synchronization history recovery.
The QEMU WAV backend records notification/preview sounds and silence under
global, app and do-not-disturb policy. PNGs, service snapshots, serial output and
audio analysis are kept outside Git in `build/qemu-preferences-test/`.

The fixed-operation privileged fixture exists only in the private cloned test
image; it is neither a production daemon nor part of the installed root filesystem.
The reference image and existing user disks are untouched by the test.
