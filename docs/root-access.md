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
misowned, symlinked, multiply linked or permissively readable state locks access
rather than resetting the password. Failures impose a durable one-second
cooldown, rising to 30 seconds after five failures, shared between callers and
retained on daemon restart.

Readers validate the root-owned policy through an opened descriptor. Atomic
replacement can remove that inode's directory entry while leaving its complete
snapshot valid; this does not revoke sessions for an unchanged generation. New
reads observe the published policy, so disable, password rotation and malformed
state still revoke sessions. Revocation reasons are recorded in rootd's service
log without passwords or verifier contents.

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
commands keep their private mount/PID/IPC namespaces, minimal device view,
Landlock and seccomp policy. The authorized root PTY is passed as a descriptor;
the terminal's own devpts instance never exposes other host PTYs. Root helpers
run in rootd's system namespaces and do not move the requesting app out of its
sandbox. Sessions and descendants use dedicated
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
workers at eight and active root sessions at six, leaving two workers for control
and revocation. It bounds initial request I/O and applies a 15-second control
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
It is an inventory, not an integrity or firmware update system. The default QEMU
boot uses the kernel on the separate EFI partition through UEFI/GRUB; replacing
a file on the root filesystem does not replace that kernel. The explicit
`LEON_BOOT=direct` diagnostic path still loads the host kernel. See
[disk-boot.md](disk-boot.md) for offline boot-image handling. Kernel installation
and OTA are outside this interface.

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
policy persistence and retained rotated passwords. Interactive checks exercise
the real Settings root switch, password purpose through the shared Chinese IME,
kernel/module/core views, service restart and policy save, and repeated `su`
commands from the sandboxed terminal. Native Weston separately covers first
password setup, rotation and editing against private kernel fixtures.
The generated reference image
and personal `build/leonmobile-user.img` remain untouched. Probe reports and serial
evidence are ignored under `build/qemu-test/`.

## Third-party single-command requests (protocol 1)

`root.exec` packages declare a separate `root.entry` ELF and a bounded purpose
(see `application-format.md`). rootd authenticates `SO_PEERCRED`, the installed
application entry's device/inode and PID start time, a fresh installer instance
ID, immutable helper path, Root generation and authd's unlocked-session epoch.
The helper is opened before confirmation and executed by descriptor. An app
cannot choose a system executable, supply environment variables, descriptors,
stdin, passwords, or run an interactive shell through this protocol.

`app_request` accepts **only** `op` and `args`: an array of at most four printable
ASCII strings, each at most 64 bytes, 160 bytes combined. The exact arguments
are confirmed by the system UI. No shell interpolation occurs. A declared helper
can implement arbitrary Root operations; review its source before approving.
Requests expire after 90 seconds. There are two application request slots, one
per UID, within the six-session shared Root limit. Two control workers remain
reserved against untrusted peers. Pending records use a private lock and atomic
files because each connection is handled in a separate process.

The original connection receives newline JSON frames: `ok/state=pending/token`,
then `state=running/pid`, zero or more `output_hex` frames (combined stdout/stderr,
lossless bytes), then `state=finished/status`. A failed request has `ok=false`
and an `error`. Applications must keep the connection open and drain output;
closing it cancels execution. The application remains at its own UID. A new
process runs the pinned helper at UID/GID 0, with no supplementary groups,
inherited application descriptors, terminal or application environment. Stdin is
`/dev/null`; cwd and HOME are `/`. Root helpers receive the verified
`LEON_CALLER_APP` and `LEON_CALLER_UID`. Execution is limited to 30 wall seconds,
30 CPU seconds and 64 KiB output. Successful exit also ends descendants.

Only the installed shell (UID 1000, executable and leond ancestry) or a Root
administration/test peer can call `app_pending`/`app_decide`. The latter takes
`token`, boolean `allow`, an optional `block` on denial, and the Root password
on approval. A successful decision is single-use and bound to the trusted UI
PID/start time. Every approval checks the password afresh, using the existing
persistent failure limits. Neither a portal grant nor a former approval permits
execution. Password changes and disabling Root revoke requests and executions.

Settings can read `app_list` (eight apps per `offset` page) and `app_history`
(eight newest events per page, 64 retained). `app_revoke` cancels pending and
running work without increasing privilege. `app_policy` takes `id`, `blocked`
and a fresh Root `password`. Restrictions persist per installation; updates and
reinstallation return to asking. No persistent allow/remember grant exists.
`apps-policy.json` is bounded to 128 identities, failing closed on corruption or
capacity exhaustion. Audit records contain identity, instance, event, timestamp
and exit status; never passwords, arguments or output. Active request arguments
are transient Root-private files, removed on completion/restart.

The Root-only broker calls `app_cancel` on stop and `app_reset` before clearing
private data or uninstalling a Root app. It waits for Root cgroup writers to
exit and refuses a successful reset if cleanup is unavailable. `leon-launchd
--root-socket PATH` selects an isolated test endpoint. rootd accepts `--root`,
`--builtin` and `--auth-socket` for isolated catalogs/authentication. Production
identity, owner/mode and unlocked-session checks are enforced in these paths;
`--test` only relaxes trusted UI executable checks, ARM64 helper machine checks
and host cgroup/management mutations.

Caller death, disconnect, update/reinstall, revocation, expiry, authd failure or
locking the device cancel the request/helper. A rootd restart kills stale Root
cgroups and drops every pending decision. Helpers use parent-death signals and
cgroup cleanup. These are reliable process-lifecycle controls for cooperative
Root helpers, not containment of deliberately hostile UID 0 code: unrestricted
Root can change the system, escape its cgroup, read secrets, and undo controls.
Revocation cannot undo changes already made. Packages remain unsigned; this is
a QEMU developer prototype with no claim of an independent security audit.

## Application SDK and system UI

Buildroot installs `libleon-root.so.1` plus `LeonRootConfig.cmake` and
`leon-root.pc` in the SDK. With C++20, use `find_package(LeonRoot 1 REQUIRED)` and
link `LeonRoot::Root`; include `<leon/root.hpp>`. Run
`leon::request_root({"argument"}, stop_token, phase_callback)` on a worker thread.
It returns bounded binary `output`, `status`, `error` and `cancelled`; a nonzero
helper exit code is a completed command, not a transport failure. A requested
`std::stop_token` shuts down the connection and cancels pending or running work.
The callback reports pending/running from the worker thread. Never block the
LMUI event loop while waiting for the user to approve.

The trusted shell paints application ID, unsigned-package source, version,
helper and exact JSON-escaped argument boundaries. It paginates long commands
before the password page, masks password entry, disables IME learning and clears
input on cancellation, focus loss, locking or service failure. Password hashing
runs asynchronously. The compositor directs modal input to the trusted shell;
the application's own window never receives Root password input. Trusted
`app_abort` cancels a token even if an asynchronous password verification just
approved it. It requires the same bound UI PID/start time and cannot authorize
anything. Settings → System → Developer options → Application Root requests
shows per-app request blocking, authenticated unblocking, immediate revocation
and recent authorization/execution records.

`examples/root-demo` is an uninstalled third-party SDK sample. Its separate
helper only prints UID/kernel diagnostics. Build it with
`scripts/build-root-example.sh`; install the generated `.lmpkg` locally when
wanted. Neither this sample nor privileged target test fixtures ship in the
normal system image.
