# Core system services (generation 1)

LeonMobile 1.0.0 remains a QEMU developer platform. These services perform native
Linux operations and use the bounded, peer-authenticated transport documented in
[interfaces.md](interfaces.md). They do not imply physical phone support.

The independent `logging` unit (`logd`), `leonlog` CLI and Settings → System →
System diagnostics use their own credential-checked endpoints under
`/run/leon-log`. See [logging.md](logging.md) for collection, bounded history,
previous-boot queries and system-log authorization. They do not inherit the
generic transport's desktop trust policy.

Services run as root, in the foreground, under leond notify readiness and a
10-second event-loop watchdog. Their entry points, service logic and shared
transport are separate source modules. `lmsys SERVICE 'JSON'` is the administrative
client. A nonzero exit means transport or service failure. The internal userspace
helper is `leon::system_request` in `leon/system.hpp`; third-party clients may use
the JSON protocol directly. Every command returns `ok`, and failed commands also
return `error`. Unknown operations and wrong field types fail.

| Service / socket below `/run/leon/` | Service unit | Persistent policy or records |
| --- | --- | --- |
| `memory.sock` | `memory` / `memd` | `/data/leon/system/memory.json` |
| `power.sock` | `power` / `powerd` | `/data/leon/system/power.json` |
| `network.sock` | `network` / `netd` | `/data/leon/system/network.json`; session DHCP state in `/run` |
| `storage.sock` | `storage` / `storaged` | Mounted volumes are session state |
| `time.sock` | `time` / `timed` | `/data/leon/system/time.json` |
| `crash.sock` | `crash` / `crashd` | `/data/leon/crashes/` |
| `portal.sock` | `portal` / `portald` | `/data/leon/system/grants.json` |

The separate `root` / `rootd` service uses `/run/leon-root/control.sock` with a
five-second watchdog and a root-owned runtime directory. Password policy,
credential boundaries, authenticated PTY sessions, kernel/module management and
persistent service overrides are specified in [root-access.md](root-access.md).
The independent `authentication` / `authd` service owns device credentials and
one-use unlock grants at `/run/leon-auth/control.sock`; its public protocol and
persistence rules are specified in [device-authentication.md](device-authentication.md).

Root elevation starts disabled; PID 1 and privileged system services keep their
required UID 0 identity.

Root and the desktop UID 1000 are trusted. Settings UID 11000 has only the
explicit additional privileges below. Application identity comes from the
validated installed package catalog. A supplied UID cannot impersonate a peer.
`--test` disables host mutations; it is intended for fixtures with private roots.

## Memory

`{"op":"status"}` returns available/total KiB, raw PSI, `psi_available`,
`min_available_mb`, `reclaimed` and `last_reclaimed`. Missing proc data is reported
as unavailable and does not cause automatic killing.

Root may set `{"op":"policy","min_available_mb":96}` (16–1024 MiB). Every two
seconds the service checks `MemAvailable`. Below the threshold it asks the broker
to checkpoint/stop one background application, then waits at least five seconds
before another successful reclaim. Foreground and legacy noncooperative apps are
excluded. Kernel OOM handling remains the final fallback. Per-application cgroup
limits and lifecycle priorities are described in [application-format.md](application-format.md).

## Power

`status` returns lock/request state, idle time/policy, inhibitors, detected
batteries, external power (`-1` unknown, `0` offline, `1` online) and the kernel's
suspend states. Applications only see their own inhibitor details. QEMU normally
has no battery; a charge estimate is not invented.

Desktop/root operations are `activity`, `lock`, `poweroff`, `reboot`, and
`suspend`. The compositor reports real pointer, keyboard and touch activity at
most once per second. `authd` owns authoritative lock state; `powerd` samples it
and delegates manual/idle locks to authentication. `activity` cannot clear an
outstanding lock. The legacy `locked` operation is rejected in production;
private `--test` fixtures retain it for isolated idle/inhibitor tests. Device
PIN/password and compositor enforcement are described in
[device-authentication.md](device-authentication.md).

Root sets `policy` with `idle_seconds` (0 disables; maximum 86400; default 300).
Idle expiry locks the authoritative authentication session. `inhibit` accepts an installed application's
nonempty `reason` (maximum 95 bytes) and `seconds` (1–300), returning a `token`.
`release` requires that token and its owning UID (or root). There are 32 global
slots, at most four per untrusted application. Expired inhibitors are removed.
Manual lock and shutdown do not depend on an inhibitor's consent.

Suspend requires a locked session, no inhibitor, and kernel `mem` or `freeze`
support. The write's actual failure is returned. Physical suspend, wake sources,
battery gauges, thermal control and phone-specific power drivers need hardware
validation. Poweroff/reboot use PID 1's coordinated shutdown path.

## Network

`status` reports actual interface addresses/link flags, IPv4 routes, resolver,
managed `eth0`, enabled policy, DHCP PID, configuration progress and errors.
`connected`/`connection_state` check that interface's carrier, address, default
route and resolver. They never infer connectivity from an unrelated interface;
Internet reachability is explicitly `unverified`.

Root/desktop/Settings UID 11000 can `configure` a nonempty `values` object with
`enabled` (boolean), `method` (`dhcp` or `static`), numeric IPv4 `address`, integer
`prefix` (1–32), optional numeric IPv4 `gateway` (empty disables the default
route), and up to three numeric IPv4/IPv6 addresses in `dns`. Unknown keys,
wrong types, nonunicast addresses and off-subnet gateways are rejected before
any change. Static mode requires a valid host address. Policy is durably stored
in `/data/leon/system/network.json`; corruption prevents startup.

Configuration success acknowledges saved desired state. Application is asynchronous:
poll `configuration_pending`, `error`, addresses and routes to observe completion.
The bounded configuration helper flushes previous addresses/routes and sets
link/static state. Failure remains visible and retries after two seconds.
DHCP runs as one supervised foreground BusyBox udhcpc process with bounded
restart backoff. `renew` signals an enabled DHCP client; it rejects static or
disabled policy. Compatible `enable`/`disable` save policy and schedule application.
Children reset inherited signal masks and die with netd.

DHCP hooks publish lease DNS to `/run/leon/dhcp-resolv.conf`. netd exclusively
publishes the system resolver atomically and world-readable, choosing configured
DNS overrides when supplied. An empty DNS array follows DHCP (or provides no DNS
in static mode). Disabling clears DNS on the next sample. The service samples
once per second and publishes `/run/leon/network-state.json`. Real changes
advance `generation`; `watch` with nonnegative integer `since` returns a snapshot
and `changed`. Clients should poll at most once per second. `--test` permits
policy validation/persistence but cannot mutate host interfaces or DNS.
There is no Wi-Fi supplicant, cellular modem manager, VPN or multi-interface
routing policy. QEMU tests exercise DHCP renewal/restart and real static/DHCP
configuration transitions.

## Storage

`status` returns actual `statvfs` capacity/read-only/low-space status for root,
data and temporary storage; sysfs block size/removable flags; ext4 error counters
when available; mountinfo; last operation error; and generation. It samples every
two seconds and publishes `/run/leon/storage-state.json`. Low space means less
than 5% or 64 MiB available. Unavailable filesystems carry an error.

Desktop/root may `mount` or `unmount` with `device` such as `sda1`. Names must be
lowercase alphanumeric and at most 63 bytes. Only sysfs-declared removable media
is accepted; the system disk is excluded. Targets are fixed at `/media/DEVICE` on
tmpfs. The supported filesystem is ext4, mounted read-only with nodev/nosuid/noexec.
Busy unmounts report failure and can be retried after handles are closed. There
is no formatting, repair, forced unmount, writable removable-media policy or
support for other filesystems. Kernel/filesystem errors are surfaced, not repaired
automatically. QEMU verifies a real USB storage device and a busy mount.

## Time

`status` returns Unix seconds, timezone, automatic policy, numeric IPv4 SNTP
server, synchronization/error state and RTC availability/time. Settings/root/
desktop may use `configure` with optional `timezone`, `automatic`, and `server`.
Zones must be installed TZif files. Configuration is durable and switches
`/run/localtime`; `/etc/localtime` points there. Long-running built-ins and the
shell follow timed's zone and call `tzset`, including under musl's timezone cache.

`set_time` takes integer `unix_seconds` in 2020–2100. `write_rtc` writes UTC to
`/dev/rtc0`. These operations report permission, hardware and I/O errors.
Automatic SNTP uses a connected nonblocking UDP socket, correlated origin fields,
server version/mode/leap/stratum checks, and the four NTP timestamps. Samples must
have bounded nonnegative network delay and at most a one-day offset. Era rollover
past 2036 is handled. Requests time out after three seconds, retry after a minute,
and successful samples refresh hourly. SNTP is unauthenticated; NTS and a full
clock-discipline daemon are not implemented. The QEMU test uses a local actual
UDP exchange; public Internet NTP availability is not assumed.

## Crashes and exit history

The kernel's `core_pattern` pipes fatal dumps to `crashd --capture %P %u %s %t`.
Four concurrent kernel dump pipes are permitted. Capture uses a private lock,
5-second input deadline and 8 MiB core limit. Eight reports are retained. Report
metadata contains PID, UID, signal, timestamp, app identity, byte count,
truncation and I/O failure. Large cores are intentionally truncated and may not
be fully usable by a debugger. Complete symbolication is outside this collector.

`list` returns report metadata and the most recent 64 durable broker exit records.
Root/desktop can see all records; apps only their own UID. `remove` is trusted-only
and takes a strict report `id`. Raw cores remain root-private. Root-only
`record_exit` accepts installed `app`, `pid` and normalized `status` (exit code or
128 + signal). The broker reports observed exits without blocking its event loop;
that delivery is best effort if crashd is unavailable. Kernel core collection is
independent of broker event delivery. Retention is bounded, and storage failures
remain visible in metadata/errors.

## Permission and resource portal

Manifests may declare only `network` and `files.import`, without duplicates.
`query`/`request` take `permission` for the caller's own installed identity. Root
may supply `uid`. Query returns `granted` and `pending`; request creates a random
confirmation token when needed. Requests expire after 120 seconds, with at most
32 slots. `pending` and `decide` (`token`, Boolean `allow`) are desktop/root only.
The app must still exist and declare the permission at decision time. Grants are
written before success is returned; malformed stored grant policy fails closed.

The trusted shell displays the app and permission. It restarts an already-running
app after a network grant, because seccomp socket policy is fixed at launch.
`revoke` requires installed `app` and declared `permission`; it persists denial
and stops the app so existing network descriptors cannot survive a successful
revocation. Failure to stop is reported and can be retried. Permissions do not
bypass private home ownership or Landlock file rules.

After files.import approval, an app uses `choose`, then polls `selection` with
its token. The desktop presents the file picker and calls `pick` (`token`, relative
`file`) or denies/cancels with `decide`. Results are visible only to that app and
trusted peers. The desktop-only `files` operation lists a relative `directory`
beneath `/data/leon/shared` (up to 128 entries per directory); the shell supports
navigation and pages of six entries. `import` is the corresponding trusted direct
operation with `uid` and relative `file`.

Selected files are opened with openat2 BENEATH/NO_SYMLINKS/NO_MAGICLINKS, must be
regular and at most 16 MiB, and are copied into the app's private `imports`
directory. Destination directory FDs, exclusive names, fsync, 0600 mode and the
app's ownership prevent symlink replacement from redirecting root writes. The
portal returns the private copy path. It never hands the app a broader filesystem
grant. The editor's Import action exercises this flow; its text loader separately
limits documents to 1 MiB. This portal is not a general device/camera/location API.

## Independent input method

`imed` is an independently supervised desktop-session service. Its engine and
Wayland input-method-v2 backend use C17; the keyboard uses C++20 LMUI and the
Material theme. The compositor bridges input-method-v2 to focused text-input-v3
clients, restricts input-method ownership to the desktop UID and one active
instance, keeps the keyboard from stealing keyboard focus, and reserves a
300-pixel bottom area above navigation. Focus changes, app exit, shell navigation
and lock deactivate the method; a restarted method can reactivate an enabled
field. Ordinary application UIDs cannot impersonate the trusted keyboard.

The shared keyboard has English, symbols, shift, backspace, enter and tone-free
pinyin (`v` for ü), preedit, exact-syllable/word candidates and candidate pages.
Unicode 17.0.0 Unihan generates 52,720 candidates across 451 syllables, plus an
original common-word supplement. Data and license hashes are pinned in the
Buildroot `leon-pinyin` package; downloaded/generated data stays out of Git. This
is basic pinyin candidate input, without adaptive phrase prediction, cloud
suggestions, handwriting or a full production language model. Password/PIN
purposes do not use the pinyin preedit engine. Applications retain responsibility
for correct UTF-8 cursor/selection editing through text-input-v3.
