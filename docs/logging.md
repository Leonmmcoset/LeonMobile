# Unified logging

`logd` is a C17 service; `leonlog` is its CLI. This is bounded diagnostic history,
not a lossless audit trail. LeonMobile remains a QEMU developer graphics prototype.

## Transport and identities

Root-owned `/run/leon-log` (0755) contains:

| Endpoint | Mode | Purpose |
| --- | --- | --- |
| `write.sock` | 0666 | Public datagrams with kernel `SCM_CREDENTIALS` |
| `capture.sock` | 0600 | Root supervisors forwarding child output |
| `control.sock` | 0666 | One newline-delimited JSON query per connection |

`src/libraries/log-client/log.h` defines local datagram ABI v1: six native-endian
uint32 words (magic `0x4c4d4c31`, UID, PID, severity, scope, message length), 96
source bytes including a NUL, and 1024 raw message bytes. Total size: 1144 bytes.
Ancillary file descriptors are rejected. Severity follows syslog, 0 (emergency)
through 7 (debug). Supervisor scopes are 1 (system) and 2 (application).

Public UID/PID/scope fields are ignored. Records use the kernel sender's UID/PID,
scope `user` and source `uid:UID/TAG`; the tag is untrusted escaped text. Root
supervisors bind captured output to the launched UID, main PID and service/app
ID. Descendant output shares that launch identity. Kernel records use `kmsg`,
the original `kernel_seq`, and UID/PID -1 because kmsg supplies no credentials.
Merged stdout/stderr is classified as informational (6); raw console text does
not provide a trustworthy severity field. Kernel priorities and explicit
`leonlog write --level ...` messages retain their supplied severity.
Control bytes, invalid UTF-8 and bidi controls are escaped; valid UTF-8 remains.
Sending is nonblocking; success means queued, not durably stored.

Readers use `SO_PEERCRED`. Ordinary UIDs, including desktop UID 1000, receive only
their own `app`/`user` records. App UID allocation is stable package policy.
Uninstall/data clearing does not erase retained diagnostic history. Root reads
all scopes. Settings gets system access only as the real UID 11000 executable
`/usr/bin/leon-settings`, directly launched by `/usr/sbin/leon-launchd`, while
authd reports unlocked. Executable, lock, auth epoch and generation are rechecked
after reading history. Auth failure denies system access. Settings diagnostics
does not require enabling Root. Root-equivalent code can access the journal.

## Query protocol

Requests are JSON objects up to 4095 bytes plus a newline. NULs, extra frames and
passed FDs are rejected. Replies are one JSON line below 768 KiB. The dedicated
client verifies root server credentials and uses a total exchange deadline.

`{"op":"query","boot":"current","limit":50,"tail":true}` returns newest
retained records, sorted ascending. Optional filters: `max_level` (0–7), `source`
(exact), `uid`, `pid`, `after` (exclusive sequence). `limit` is 1–128. `boot`
accepts `current`, `previous`, or a 36-byte boot ID. `previous` selects the most
recent different boot still present; a complete previous boot is not guaranteed.

Records contain `boot`, `seq`, `realtime_ms`, `monotonic_ns`, `level`, `scope`,
`source`, `uid`, `pid`, `kernel_seq`, `kernel_monotonic_us`, `truncated`, and
`message`. Wall/monotonic timestamps describe ingestion; `kernel_monotonic_us`
preserves the original kernel timestamp for replayed boot messages (-1 otherwise).
Cursors increase within a boot,
including across daemon restarts; they are not consecutive message counts.

Replies contain `access` (`own`/`system`), `records`, `selected_boot`, `next`,
`more`, `tail_limited`, `retention_limited`, `gap`, and `history_unavailable`.
Follow uses an initial tail then `after:next`, `tail:false`; consume pages while
`more:true`. Reset the cursor on a boot change. `gap:true` conservatively means
the cursor predates retained history. Empty pages keep the cursor to avoid
skipping queued records. Do not assume completeness after loss or eviction.

`{"op":"status"}` reports access and limits. System readers also see kernel
availability, accepted/invalid counts, rate drops, evictions, queue/write drops,
writer errors and storage health. Daemon counters reset on logd restart; writer
counters reset on worker restart. Writer sequence acknowledges an append, not
an fsync; fsync runs once per second while storage responds.
`{"op":"crashes"}` proxies crashd metadata for system readers, without core
contents. `{"op":"services"}` returns leond's bounded status snapshot as a
`services` array (`name`, `state`, `pid`), also restricted to system readers.
Failures/unknown operations return `ok:false` and `error`.

## Limits and failure handling

Separate rings retain 256 system/kernel and 128 app/user records, each below
8192 serialized bytes. The writer IPC queue requests a 256 KiB send buffer
(subject to the kernel's socket-buffer cap and accounting).
Queue overflow loses persistence work while memory remains queryable. Public
and app sources have per-UID quotas (burst 128, approximately 62 messages/s,
128 active buckets). Public and supervisor sockets have separate kernel queues
and receive budgets. Supervisors use nonblocking pipes and bounded drain work.
Each stream keeps four offline fragments (at most 1024 raw bytes each), plus a
loss count; old fragments are retried after logd recovery. leond retains closed
service buffers. launchd retains at most 16 closed application buffers and reports
an eviction to its own service output. Observed transport losses emit a warning
when the receiver recovers. Nonblocking stdout writers must handle short writes
and EAGAIN; bytes abandoned by an application cannot be counted by the supervisor.
Long/no-newline output is fragmented into chunks of at most 1024 bytes. Production
passes `--log-socket /run/leon-log/capture.sock` to both supervisors; isolated native
callers without this option retain their legacy file-output behavior.

A separate writer owns disk I/O. Root-owned `/data/leon/logs` (0700) holds four
segments `journal.0`–`journal.3`, each at most 1 MiB, mode 0600. Files must be
regular, root-owned, single-linked, correctly permissioned and within limits.
Symlinks are rejected. An incomplete last line is repaired before appending.
Rotation fsyncs the old segment and directory. Storage failures retry and report
degraded health. A dead writer restarts; a stopped writer cannot accumulate
replacement processes or block ingestion and watchdogs.

At most six query workers run, with two slots reserved for trusted diagnostics.
Request collection expires after 700 ms; workers have 2.5 seconds overall and
one second for sending. Reads scan at most four bounded files. Slow disks and
clients cannot block the ingestion loop. History under rotation is best-effort;
memory/disk records deduplicate by boot/sequence. Memory-only entries disappear
if logd dies. This does not promise lossless logging under floods or power loss.

`kernel.cursor` in `/run/leon-log` suppresses kmsg replay after daemon restart.
The tmpfs checkpoint is best-effort; abrupt failure can repeat a kernel record,
identifiable through `kernel_seq`. Test-only `--test` relaxes Settings executable
identity (never UID/lock/epoch), disables host kmsg by default, and permits a
synthetic `--boot-id`. The installed service never uses this option.

## CLI

```sh
leonlog                              # newest records visible to this UID
leonlog --follow
leonlog --level warn --json
leonlog --previous --source apps
leonlog --uid 11000 --limit 128
leonlog status
leonlog crashes
leonlog write 'Checkpoint reached' --source example --level info
```

`--socket`/`--ingest` select test endpoints. Write messages are limited to 1024
raw bytes. A terminal needs separately authorized Root to read system logs;
UID 1000 and terminal apps are not automatically privileged.

## Settings

Settings → System → System diagnostics shows the journal health and loss count,
latest 64 records, severity and current/previous-boot filters, optional automatic
refresh, full record details, read-only service state and crash metadata. Use
`leonlog` for PID/UID/source filters, paging and cursor following. Diagnostics
queries and authentication status checks run off the UI thread. Back remains
available during a query; late responses from another tab/filter are discarded.
Diagnostic data is excluded from saved application state, cleared on focus loss,
and cleared if authd locks, restarts or becomes unavailable. The page disables
core dumps for the Settings process. The compositor remains responsible for
blocking input and covering private surfaces during lock.

## Validation commands

After building the host binaries and target image:

```sh
python3 -m unittest discover -s tests -p test_logging.py -v
./scripts/test-wayland.sh
python3 scripts/test-logging-wayland.py
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --system-probe build/system-probe --core-only --timeout 300
python3 scripts/test-qemu.py --logging-only --timeout 180
```

The real-peer/native diagnostics fixtures require root; target tests use private
disk copies. `--logging-only` uses the production image without a privileged boot
fixture, performs a real kernel reboot, and captures the prior boot's logs through
the actual Settings process. Evidence stays under ignored `build/` directories.
