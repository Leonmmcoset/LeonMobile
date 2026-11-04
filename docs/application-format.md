# Native application format 1

Applications execute as little-endian AArch64 ELF binaries against the LeonMobile musl SDK. ELF is the executable format; `.lmpkg` is the distribution container. glibc and Android binaries are not compatible merely because they are ELF.

An application package is a gzip-compressed USTAR archive containing regular files only:

```text
manifest.json
checksums.json
bin/hello
resources/...
```

`manifest.json` uses the following fields:

```json
{
  "format": 1,
  "id": "org.example.hello",
  "name": "你好 LeonMobile",
  "name_en": "Hello LeonMobile",
  "version": "1.0.0",
  "arch": "aarch64",
  "lmui": 1,
  "entry": "bin/hello",
  "icon": "gallery"
}
```

`id` is a lowercase dotted identifier, maximum 96 bytes. Versions are three numeric components, without leading zeros, each at most six digits. `entry` is relative to the package root and must identify a regular little-endian AArch64 ELF executable. Optional `args` is an array of at most 16 strings. `name_en` and `icon` are optional presentation metadata. Built-in manifests are installed separately and use `/usr/bin/` executable paths and stable built-in identities.

Optional `"lifecycle":1` opts into the private launcher event channel for
foreground/background/checkpoint/shutdown requests, described in
[interfaces.md](interfaces.md#application-lifecycle-v1). Existing format 1
packages without this field continue to run. Current LMUI consumes the channel
automatically and offers `Application::on_lifecycle` for app-owned state saves.

Each built-in has its own ELF entry (`/usr/bin/leon-settings`, `leon-calculator`,
`leon-gallery`, `leon-files`, `leon-editor`, `leon-terminal`, `leon-images`,
`leon-clock`). The former `leon-app MODE` dispatcher is retired. Built-in IDs,
UIDs and private data paths are unchanged. Each application's manifest is owned
alongside its implementation in `src/apps/NAME/manifest.json` and installed as
`/usr/share/leon/apps/NAME.json`.

`checksums.json` maps every regular archive file except itself to its lowercase SHA-256 digest. Missing or extra coverage, duplicate files, absolute paths, parent/dot path components, backslashes, links and special files are rejected. Archives are limited to 4096 regular files, 128 MiB per file and 256 MiB total extracted regular data. These are integrity checks; packages are unsigned in 1.0.0.

`scripts/make-package.py MANIFEST PAYLOAD OUTPUT` creates a deterministic archive with normalized tar/gzip metadata. Payload directories must contain only files and directories, excluding the reserved metadata filenames. Non-system shared libraries may be included, but the application must link them with a suitable relative RPATH such as `$ORIGIN/../lib`. The installer does not solve dependencies or synthesize a library search path.

## Installation and lifecycle

The root-owned store is `/data/leon`. An exclusive installer lock serializes writers. Extraction and validation happen under `staging/`. Only after full validation is a version moved into `apps/ID/versions/VERSION`; an atomic rename then switches `apps/ID/current`. Interrupted staging and unused interrupted publication directories are recovered during a later install. The active version is preserved when validation fails.

Upgrades must increase the version. Built-in apps cannot be replaced or removed by local packages. IDs retain their assigned UID across uninstall/reinstall. The launch service pins the executable's resolved version path and does not change an already-running application when an upgrade is installed; restart that application to use the new version. Old versions remain on disk in this prototype.

App data is `/data/leon/data/ID`, owned by that app's UID with mode 0700. Normal removal preserves data. `--purge-data` removes it. Uninstall through the broker stops the running app before removing its files. There is no persistent application session restoration across broker restarts.

```sh
lmpkg list
lmpkg running
lmpkg install ./org.example.hello-1.0.0.lmpkg
lmpkg launch org.example.hello
lmpkg remove org.example.hello
lmpkg remove org.example.hello --purge-data
```

On-device installation/removal by the trusted `leon` desktop user goes through the broker. UID 0 may also operate locally. `--root DIR` creates an isolated offline store for tooling and tests and uses `DIR/builtin` for any built-in test manifests.

## Declared capabilities

An optional `permissions` array may contain `network` and `files.import`, at most
once each. Unknown values, duplicate entries and non-string values are rejected.
An omitted permission is denied. Declaring a capability does not grant it; an
installed app requests it from the independent permission portal and the desktop
confirms it. See [core-services.md](core-services.md) for requests, decisions,
revocation and selected-file copies. The terminal declares network access, and
the editor declares file import; neither declaration is an automatic grant.

## Kernel enforcement at launch

On the reference target the broker waits for cgroup setup and an exec/setup
acknowledgment before returning a successful launch. Required isolation failure
is an error. Each application UID has a cgroup v2 subtree with `memory.max` =
256 MiB, `memory.oom.group` = 1 and `pids.max` = 128. These limits include children.
The whole cgroup is killed on app exit/forced stop, including children that create
new sessions. Broker startup removes stale app processes left by an earlier
broker crash. Foreground/background OOM priorities and cooperative checkpointing
remain part of the lifecycle protocol.

Landlock ABI 3 or later restricts writes to the app's home/runtime and the
explicit PTY/null/random devices. Read/execute rules cover installed applications,
OS libraries/programs, configuration, proc/sysfs and the public service directory;
ordinary Unix permissions still apply. Other private homes and shared documents
cannot be opened directly. `/tmp` is not granted; `TMPDIR` is the app's private
`.tmp` directory. Apps should use HOME/XDG directories rather than shared scratch
paths. Selected files arrive through the portal as private copies.

The inherited seccomp policy permits AF_UNIX and grants AF_INET/AF_INET6 socket
creation only when the manifest declares network and the portal has approved it.
It denies netlink/packet sockets, ptrace, namespace creation, mount/privilege
interfaces, dangerous kernel administration syscalls and io_uring's alternate
socket path. The audit architecture and x32 syscall range are checked. Open-file
limit is 512 and core limit is 8 MiB. Policies are inherited across fork/exec.

This is an actual kernel-enforced developer policy with stated boundaries.
AF_UNIX IPC and the Wayland server are shared; each privileged service must still
authorize peers. Publisher signatures, full per-app namespaces, credential-based
screen unlock, hardware device permission APIs and an adversarial security audit
are outside this implementation. A granted app can communicate with other apps;
this is not a claim of complete information-flow isolation. Explicit development
roots (`leon-launchd --root`) bypass target cgroups/Landlock to support native host
fixtures; distribution services never pass that option.
