# Platform services, protocol generation 1

The services in this document use the bounded, credential-checked C17 system
transport in `interfaces.md`. Client-supplied UIDs never select a caller. Native
fixtures use private roots and real Unix peer credentials. These protocols are
for the LeonMobile 1.0.0 developer graphics prototype.

## Settings

`settingsd` owns the five public UI preferences: boolean `dark` and
`reduce_motion`, `language` (`zh-CN`, `en`), `navigation` (`gestures`, `buttons`),
and integer `volume` (0–100, preferred playback volume). It stores a versioned
record with a durable generation in `/data/leon/system/settings.json`.
Root, the desktop UID 1000 and Settings UID 11000 may write; reads contain only
these nonsecret preferences. Authentication, root passwords and grants remain
in their dedicated services.

* `status` returns `settings` and `generation`.
* `schema` returns types, defaults, ranges and enum choices.
* `set` accepts `key`, `value`; `update` accepts a nonempty `values` object.
  Optional `expected_generation` implements compare-and-set. A batch is checked
  completely before any write. Unknown keys and wrong types fail.
* `watch` accepts nonnegative integer `since`, returning the current snapshot and
  `changed`. Poll at most once per second; disconnected clients can resynchronize
  without losing their last generation. Repeating an unchanged value does not
  advance the generation.

The first initialization migrates validated values from the legacy
`/data/leon/settings.json`, filling missing defaults. A malformed existing record
prevents startup instead of resetting user preferences. Replacements use fsync,
rename and parent-directory fsync; an I/O failure is reported. The executable is
introduced separately from its supervised distribution integration.

## Documents

`documentd`, `/run/leon/documents.sock`, provides temporary read-only document
capabilities. `choose` requires an installed identity declaring `files.import`;
the trusted shell selects each individual file. This selection itself authorizes
that document, without giving the application directory access. The existing
permission portal's copy/import API remains compatible.

* `choose` creates a picker request and returns a 128-bit random hex `token`.
  Optional integer `seconds` is 1–600 (default 300), including selection time.
* Desktop/root `pending`, `files` (`directory` relative to `/data/leon/shared`),
  `pick` (`token`, relative `file`) and `cancel` implement the trusted picker.
* `selection` returns `state` (`choosing`, `ready`, `canceled`), `done`, display
  `name`, byte count and remaining lifetime. Tokens are visible only to their
  owner or trusted peers. Caller-supplied UIDs never override ownership.
* `read` takes `token`, nonnegative integer `offset` and `length` 1–2048, returning
  binary-safe lowercase `hex`, the next `offset` and `eof`.
* `offer` snapshots a relative `file` beneath the caller's own private home. It
  lets an app share its own document without exposing a private path to peers.
* `revoke` invalidates the owner's capability and all delegated descendants.
  Root-only `delegate` additionally takes verified `from_uid`, `to_uid`, and
  returns a distinct capability for the recipient. Root-only `reset_app` is the
  application broker's clear/uninstall hook. `list` shows the caller's grants.

Files must be regular, at most 16 MiB, and are opened beneath a directory FD with
openat2 NO_SYMLINKS/NO_MAGICLINKS. A sealed anonymous snapshot prevents changes
after selection. At most 32 capabilities, four per app and 64 MiB of accounted
snapshots are allowed. Grants expire, disappear on daemon restart and are bound
to the installer's instance marker (including same-version reinstalls).
Revocation blocks subsequent reads; bytes an app has already read cannot be
recalled. Applications never receive a raw file descriptor that bypasses later
revocation checks. Executable startup integration follows in a separate step.
