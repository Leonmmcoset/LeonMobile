# Device authentication

LeonMobile 1.0.0 is a QEMU developer graphics prototype. Device credentials belong
exclusively to `authd`, independent of the root password and root opt-in policy.
No PIN is preinstalled. A PIN has 6–12 decimal digits; a password has 8–128 UTF-8
bytes without control characters. Credential changes and removal verify the
current device credential. A malformed policy fails closed and is never replaced
with an empty credential.

## Service and persistence

The `authentication` notify service runs `/usr/sbin/authd` as root after `prepare`,
with a 5-second event-loop watchdog. Its root-owned directory is
`/run/leon-auth/` (0755), containing `control.sock` (0666). Socket permissions allow
connections, not authorization. The daemon authenticates Unix peer credentials,
installed executable inode/device and its supervised parent:

| Role | UID | Executable | Parent |
| --- | --- | --- | --- |
| Shell | 1000 | `/usr/bin/leon-shell` | `/usr/sbin/leond` |
| Compositor | 1000 | `/usr/bin/leon-compositor` | `/usr/sbin/leond` |
| Settings | 11000 | `/usr/bin/leon-settings` | `/usr/sbin/leon-launchd` |
| Privileged OS services | 0 | Trusted system process | — |

Ordinary applications and copies of trusted executables launched from a terminal
are rejected. UID 0 is the system-administration trust boundary; it still needs
the device password to obtain an unlock grant when one is configured. Root policy
is not implicitly enabled by device authentication.

`/data/leon/authd/` is root-only (0700). `policy.json` is a single-link regular
0600 file, updated by write/fsync/rename/directory-fsync. A singleton file lock
prevents two writers. The policy records a random 16-byte salt and 32-byte scrypt
verifier (`N=32768, r=8, p=1`); passwords are never stored or logged. Core dumps and
ptrace access to authd are disabled. One bounded worker performs derivation while
the main thread continues monitoring the lock and serving status.

A failed attempt applies a 1-second delay; the fifth consecutive failure applies
30 seconds, then doubles to a maximum of 5 minutes. Attempts during a delay do
not run the KDF. Failures and the remaining penalty are persisted before reply.
Deadlines use `CLOCK_BOOTTIME`, so wall-clock changes cannot shorten a delay. A
same-boot daemon restart preserves the deadline; a new kernel boot reapplies the
saved penalty. Completed delays are cleared durably while the daemon runs. A
successful authentication clears the consecutive-failure counter. A policy
write failure immediately invalidates authentication and locks the session.

## Protocol version 1

The transport is one UTF-8 JSON object plus newline, at most 4095 bytes including
the newline; replies follow the same framing. Ordinary connections carry one
request/reply and close. Half-closing the request stream is supported. Embedded
NULs, ancillary descriptors, oversized frames and pipelined requests are rejected.
Partial frames expire after 1 second. There is only one in-flight derivation;
additional authentication requests receive a busy error. Monitor capacity is
reserved against slow unprivileged clients.

Every reply has `ok`; failures include `error`. Successful state replies have
`locked`, `configured`, `kind`, `epoch`, `generation` and `retry_after_ms`.
`epoch` is a random 128-bit lock generation, independent of policy `generation`.
Clients must treat unavailable/invalid state as locked, never as no credential.

| Operation | Fields | Caller / result |
| --- | --- | --- |
| `status` | — | Trusted roles; current policy and lock state |
| `lock` | — | Shell, compositor, privileged OS; rotates epoch, revokes all grants |
| `watch` | — | One compositor connection; attaches locked, streams state every 250 ms |
| `authenticate` | `secret`, `purpose:"unlock"` | Shell; returns a one-use `token`, keeps the session locked |
| `authenticate` | `secret`, `purpose:"settings"`, `action` | Settings in an unlocked session; returns a grant bound to that PID/UID and action |
| `consume` | `token`, `purpose`, optional `action` | Compositor for unlock, originating Settings process for settings; consumes the grant |
| `configure` | `secret`, `kind:"none"\|"pin"\|"password"`, `new_secret` | Settings in an unlocked session; verifies current credential, atomically changes policy |
| `verify_peer` | `pid`, `uid`, `role:"shell"\|"ime"` | Compositor only; verifies a Wayland/control peer through root-owned `/proc` access |

`secret` is empty for the initial `none` policy. `new_secret` must be empty for
`kind:"none"`. `action` is at most 64 bytes. Unlock grants expire after 15 seconds
and bind to the lock epoch, policy generation and purpose. Settings grants also
bind to PID, UID and action. Replays, cross-purpose consumption and stale epochs
fail. A lock during derivation cancels credential changes and prevents issuance
of an unlock grant for the old epoch. Disconnecting the compositor monitor locks
the session and revokes grants. The monitor sends at most one command at a time
and continues reading unsolicited state frames while awaiting the response.

`leon-auth-client` is an internal C/C++ adapter, not an application SDK authority.
It validates the socket server's UID 0 and uses nonblocking connects and bounded
I/O deadlines. The daemon's `--test` mode substitutes UID-based shell/Settings
fixtures and permits a fixture boot ID; it is never used in the installed unit.

## Verification

`tests/test_authentication.py` covers real peer UIDs, production impersonation,
PIN/password rotation/removal, private salted persistence, crash/new-boot retry
limits, grant lifetime/replay/epoch/purpose/PID binding, monitor responsiveness,
unsafe policy files, malformed input and descriptor rejection.

`./scripts/build-target-probes.sh` builds `build/auth-probe`. Pass
`--auth-probe build/auth-probe` to `scripts/test-qemu.py` to inject native protocol
checks into a disposable disk, ordered before the compositor starts. Test helpers
are never installed in the distribution image. Build logs and disk evidence stay
under ignored `build/` and `output/` directories.

## Lock screen and trusted input

Settings → System → Security & screen lock configures, changes and removes the
credential. Every credential field is private and masked; secret fields are
cleared on submission, cancellation, focus loss and lock epoch changes. Shell,
Settings credential entry, the authentication daemon and shared IME disable core
dumps and same-UID ptrace access. Password derivation runs asynchronously for the
UI; errors and cooldown countdowns remain visible without echoing the input.

The compositor monitors authd on one root-verified connection. It starts locked;
EOF, malformed state, a missing heartbeat for 1.25 seconds, or losing the trusted
shell keeps/returns it to the locked state. A compositor-owned opaque scene
rectangle covers the display while every application tree is disabled. Locking
cancels keyboard/pointer grabs, clears focus and the clipboard, and hides app
popups. Keyboard shortcuts cannot exit the compositor or focus apps while locked.

`/run/leon/wm.sock` accepts control only from the actual supervised shell (or
root). Bare `unlock` has no effect. `unlock TOKEN` is forwarded asynchronously
for authd to consume; the compositor reveals apps only after a matching live
monitor confirms successful consumption. Each fresh lock epoch revokes any old
unlock attempt. Repeated unlocked heartbeat frames do not authorize a transition.

The shell paints a full opaque lock page, discards saved transition patterns and
uses LMUI's `after_commit` barrier before sending `lock-ready EPOCH`. The
compositor reveals this trusted shell only for its current epoch. A hidden shell
receives bounded frame callbacks so it can finish an interrupted animation and
submit its lock page. A stopped/crashed shell cannot reveal an old application
buffer. Role checks also validate shell and IME Wayland identities; claiming the
same UID/app_id is insufficient. The verified IME may appear over a ready lock
page for private password entry. Lock-screen notifications and permission dialogs
remain hidden. With no configured credential, swipe still obtains and consumes
a one-use grant rather than changing lock state directly.

Powerd delegates manual/idle locking to authd and rejects legacy `locked` reports
in production. User activity does not cancel a lock. Restarting either authd or
the compositor starts a new locked session; credentials persist separately.

For full target security checks, build probes and run:

```sh
python3 scripts/test-qemu.py --timeout 240 --auth-only \
  --auth-probe build/auth-probe --root-probe build/root-probe
```

The disposable image includes a test-only root broker client with a fixed fixture
password for controlled fault injection. It is never installed in product images.
A forged shell/IME client tests actual Wayland role rejection. The focused suite
exercises Settings, touch PIN entry, private IME password input, application
concealment, stale buffers, wrong credentials, crashes/stalls and a kernel reboot.
Native Settings input can be checked with
`python3 scripts/test-wayland-input.py --security`; its `LEON_AUTH_SOCKET` override
selects a private root-verified fixture endpoint, never a production service unit.
These checks validate the QEMU reference platform, not physical phone security or
resistance to an administrator who already controls UID 0.
