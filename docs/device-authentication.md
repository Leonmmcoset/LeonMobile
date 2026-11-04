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
