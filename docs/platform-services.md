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
