# Developer root access

The reference image locks the root account for conventional password login.
Buildroot root login is disabled explicitly: setting the password string to `!`
would hash that literal string rather than lock the account. Image finalization
also locks a previously generated root entry when rebuilding an existing tree.
The developer serial console continues to log in as the non-root `leon` account.
PID 1 and privileged system services retain the UID 0 privileges needed to boot
and manage the system.

The image does not enable the sudo package. Its `sudo` and `sudoedit` policy
entry points always reject execution with status 126, including when invoked
by UID 0. BusyBox `su` and `sulogin` are excluded to prevent alternate built-in
password entry points. The non-setuid `/usr/bin/leonsu` is installed as `/bin/su`.
It delegates elevation to the independent `rootd` service; it does not change its
own Unix credentials or rely on the locked shadow password.

The policy applies to the shipped system. A user deliberately given unrestricted
UID 0 can modify its implementation; Linux cannot prevent unrestricted root
from replacing the policy with their own code.

The default-policy step passed its image-finalization fixture, an offline check
of the generated ext4 root entry, and the complete QEMU suite with all three
probes and the SDK package. The target checks execute both sudo rejection paths
and verify that BusyBox exposes neither su nor sulogin. Evidence remains ignored
in `build/root-policy-*.log` and `build/qemu-test/`.

## Policy and authorization

`rootd` is a C17 notify/watchdog service supervised by the `root` leond unit.
Its socket is `/run/leon-root/control.sock`, in a root-owned 0755 directory,
separate from the desktop-owned `/run/leon`. Clients verify the server's UID 0
with `SO_PEERCRED` before sending a password. The daemon uses the same kernel
credentials to authorize callers; supplied UIDs and PIDs confer no privilege.

Settings uses **System → Developer options**, visible directly. The first enable
requires an 8–128-byte password without control characters; subsequent enable,
disable, password rotation and mutations require the current password. Closing
the switch retains the password. State is stored atomically with fsync under
root-only `/data/leon/rootd/` (0700), with `policy.json` at 0600. Only a random
16-byte salt and scrypt-derived 32-byte verifier are stored (N=32768, r=8, p=1,
64 MiB memory limit); comparison is constant-time. Policy generation changes
invalidate existing sessions. Missing state starts disabled; malformed,
misowned, linked or permissively readable state locks access rather than resetting
the password. Failures impose a durable one-second cooldown, rising to 30 seconds
after five failures, shared between callers and retained on daemon restart.

Only root or UID 11000 running the installed Settings executable (device/inode
checked through `/proc/PID/exe`) can inspect/manage developer settings. Read-only
inspection remains available while root is disabled. Only the terminal UID
11005, developer serial-console UID 1000 and root can request root PTYs. Other
apps are denied even with the right password. A child of Settings does not gain
its executable-based management identity. These are prototype system identities,
not a general third-party root grant interface.

`su`, `su -`, and `su -c 'COMMAND'` authenticate each session. `su --status` shows
the policy. Passwords are read from the controlling `/dev/tty`, with echo off;
arguments, environment variables and piped passwords are unsupported. Terminal
settings are restored on completion or cancellation. Core dumps are disabled
for the broker and credential clients; Settings clears its password fields on
cancel, submit, exit and focus loss. Passwords are not application saved state.

The broker creates a separate controlling PTY as UID/GID 0, clears supplementary
groups and inherited environment, uses a private root home, and passes the master
FD through `SCM_RIGHTS`. Window resizing, interactive job control and command exit
status are retained. A fresh broker child allows deliberate root administration
without inheriting the terminal's Landlock/seccomp restrictions. Normal terminal
commands keep their existing sandbox. Sessions and descendants use dedicated
`/sys/fs/cgroup/leon/root/sPID` groups; disconnect, disable, password rotation,
policy corruption, daemon shutdown and restart clean up these groups, including
detached children. Root can deliberately escape/modify the security policy; the
revocation guarantee concerns processes remaining in the broker's cgroup.

## Local developer protocol

One connection carries one newline JSON request (maximum 8191 bytes); replies
contain `ok`, with `error` on failure. Unknown operations and bad fields fail.
`exec` returns a PTY FD in the first successful reply, followed by a second JSON
frame containing `status` on command exit. Keep the connection open for the
session; incoming descriptors are never accepted by the server. The daemon caps
workers at eight, bounds initial request I/O, and applies a 15-second control
deadline; long-running authenticated sessions keep the watchdog responsive.
There is no password/session cache. `LEON_ROOT_SOCKET` selects an isolated test
endpoint for clients; the UID 0 peer check still applies.

| Operation | Fields |
| --- | --- |
| `status` | None; returns `enabled`, `password_set`, `sudo_enabled:false`, `retry_after_ms` |
| `enable` | `password` (new on first use, current thereafter) |
| `disable` | Current `password` |
| `password` | Current `password`, `new_password` |
| `exec` | Current `password`; optional `command`, `rows` (2–512), `columns` (8–1024) |
| `inspect` | `area`: `kernel`, `modules`, `services` or `core`; optional `offset` (12 entries/page) |
| `sysctl` | Current `password`, `area:"kernel"`, relative `path`, single-line `value` (≤256 bytes) |
| `module` | Current `password`, `area:"modules"`, module `name`, `action:"load"|"unload"` |
| `service_control` | Current `password`, `area:"services"`, service `name`, `action:"start"|"stop"|"restart"` |
| `service_config` | Current `password`, `area:"services"`, service `name`, boolean `autostart`, `restart`, `restart_limit` |

Kernel inspection takes a relative `path` below `/proc/sys`; writes use
`openat2` beneath that directory without symlinks or traversal. Changes affect
the current boot. A parameter's mode only indicates potential writability;
the kernel can still reject it. Modules show `/proc/modules` or the matching
`/lib/modules/$(uname -r)/modules.dep` catalog (`available:true`). Loading and
unloading use fixed `modprobe` argument vectors, with no forced unload.

Service inspection takes an optional `name`, returns the real leond state and
installed command/identity/health configuration. Authenticated configuration
persists only `autostart`, `restart` (`never`, `on-failure`, `always`) and
`restart_limit` (0–60000), in root-only
`/data/leon/developer/services/NAME.json`. `leonctl reload NAME` is root-only,
loads the validated override and updates restart behavior immediately, without
changing whether the current service is wanted. Autostart applies on next boot;
disabled prerequisites can block dependent services. Invalid boot overrides are
logged and fall back to built-in policy. Service control acknowledges a queued
request; use status/refresh to observe readiness or failures. The UI cannot stop
or reconfigure `root` itself; an authenticated root shell can administer it.

Core inspection reports the actual kernel release/architecture/command line and
presence/size of init, leond, rootd, app broker, LMUI, musl, compositor and shell.
It is an inventory, not an integrity or firmware update system. QEMU loads its
kernel from the host `-kernel` argument: replacing a file inside the device does
not replace that boot kernel. Kernel-image installation and OTA are outside this
interface.

## Reproducing root tests

`make test` runs private state/proc/service fixtures and real Unix-credential/PTY
tests as root in the disposable development environment. `rootd --test` requires
private proc/modules paths, bypasses only the installed-Settings executable check
for UID 11000, and refuses host module mutation. Test shells inspect private data
and their own credentials. Production cgroup/session behavior is tested on QEMU.

```sh
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 240 --root-only --root-probe build/root-probe
```

Only a private disk receives the probe and harmless sleep service. Three boots
exercise default lock, forged identities, cooldown, real sysctl/module changes,
service overrides, PTY status, cgroup descendant revocation, enabled/disabled
policy persistence and retained rotated passwords. The generated reference image
and personal `build/leonmobile-user.img` remain untouched. Probe reports and serial
evidence are ignored under `build/qemu-test/`.
