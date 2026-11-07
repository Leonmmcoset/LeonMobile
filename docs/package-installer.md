# Graphical application installer

The trusted `org.leon.installer` built-in owns UID 11008. It receives
`application/vnd.leonmobile.package` documents through the existing document and
interaction services. Files uses this type for `.lmpkg`; neither a private path
nor an executable command is passed between applications. The document limit
is 16 MiB of compressed package data. CLI installation retains the package
format's existing expanded-size limits.

The installer displays the package's declared name, ID, architecture, version,
local document source, SHA-256, requested permissions and any Root helper
purpose. An update also shows the installed version and newly declared
permissions. Packages in format 1 are unsigned: an integrity check does not
authenticate their publisher. Confirmation grants no runtime permissions and
does not enable Root. Normal portal and fresh Root authorization remain required.

Open a `.lmpkg` in Files, or open **App installer → Choose package** and use
the trusted system document picker. Review the source, version and permissions,
then press **Install** or **Update**. **Cancel** discards the review without
installing anything. After success, press **Done** and launch the application
from the home screen. Updates preserve its identity and private data; an
already running old version must be closed and reopened. Failed or damaged
packages show an error and leave the active version unchanged. A broker
restart or expired review requires a fresh review before confirmation.

The installer copies the selected document asynchronously and releases its
temporary document capability after the broker snapshots it. Files releases
its offered package capability and interaction after the receiver acknowledges it,
so repeated package opening does not consume the interaction quota.

## Broker transaction protocol

`apps.sock` accepts these operations only from root or installer UID 11008;
ordinary apps, Files, Settings and the desktop cannot confirm a review through
this interface. The existing root/desktop CLI `install` interface remains.

| Operation | Fields | Result |
| --- | --- | --- |
| `package_prepare` | none | Snapshot private `incoming.lmpkg`, return random `token` and `checking` |
| `package_status` | optional `token` | Current state, review and any failure |
| `package_commit` | matching `token` | Explicitly confirm an installable `ready` review; return `installing` |
| `package_cancel` | optional matching `token` | Cancel unpublished review; refuses once committing |

The source is opened beneath the installer's private directory using
`openat2` with no symlinks or magic links. It must be an installer-owned regular
file, with one link and size 1..16 MiB. The broker copies it into a sealed memfd;
replacement of the original cannot change either the reviewed or installed
bytes. No request field selects an arbitrary host path.

One review exists at a time and expires after ten minutes. States are `idle`,
`checking`, `ready`, `installing`, `installed`, and `failed`. Failure is a completed
operation with `ok:true`, `state:failed` and an `error`; invalid/unauthorized
requests have `ok:false`. Clients poll after a lost reply rather than assuming
that installation did not happen. A restart discards unconfirmed reviews and
never automatically commits. After an interrupted confirmed transaction, inspect
the installed version before retrying; the package writer retains its atomic
active-version switch and stale-staging cleanup.

Archive validation and installation run in a single supervised child, leaving
the broker event loop available for lifecycle and control requests. The child
retains only its result pipe and sealed archive descriptors, has bounded address
space/CPU and a 90-second wall deadline, and dies if the broker dies. It uses no
shell. Target Linux must support `openat2`, memfd sealing, pidfds and close_range.

Inspection performs the same full manifest, checksum, archive-path/link, size
and AArch64 ELF validation as installation without publishing any application.
Inspection and installation serialize with other writers using `installer.lock`.
Commit compares the installed manifest digest and installation instance under
that lock; an intervening update/uninstall/reinstall requires a new review.
Built-ins cannot be replaced; same-version installation and downgrade are
refused. Existing data/UIDs survive upgrades. Already running apps retain their
pinned old executable until restarted; old package versions remain on disk.

`lmpkg --root DIR inspect FILE` exposes the same review for offline tooling;
root can also run `lmpkg inspect FILE`. The package library's `inspect` and
`install_reviewed` adapters are internal distribution interfaces, not a new
application capability.
