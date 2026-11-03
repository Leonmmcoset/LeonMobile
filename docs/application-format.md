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

`checksums.json` maps every regular archive file except itself to its lowercase SHA-256 digest. Missing or extra coverage, duplicate files, absolute paths, parent/dot path components, backslashes, links and special files are rejected. Archives are limited to 4096 regular files, 128 MiB per file and 256 MiB total extracted regular data. These are integrity checks; packages are unsigned in 1.0.0.

`scripts/make-package.py MANIFEST PAYLOAD OUTPUT` creates a deterministic archive with normalized tar/gzip metadata. Payload directories must contain only files and directories, excluding the reserved metadata filenames. Non-system shared libraries may be included, but the application must link them with a suitable relative RPATH such as `$ORIGIN/../lib`. The installer does not solve dependencies or synthesize a library search path.

## Installation and lifecycle

The root-owned store is `/data/leon`. An exclusive installer lock serializes writers. Extraction and validation happen under `staging/`. Only after full validation is a version moved into `apps/ID/versions/VERSION`; an atomic rename then switches `apps/ID/current`. Interrupted staging and unused interrupted publication directories are recovered during a later install. The active version is preserved when validation fails.

Upgrades must increase the version. Built-in apps cannot be replaced or removed by local packages. IDs retain their assigned UID across uninstall/reinstall. The launch service pins the executable's resolved version path and does not change an already-running application when an upgrade is installed; restart that application to use the new version. Old versions remain on disk in this prototype.

App data is `/data/leon/data/ID`, owned by that app's UID with mode 0700. Normal removal preserves data. `--purge-data` removes it. Uninstall through the broker stops the running app before removing its files. There is no persistent application session restoration across broker restarts.

```sh
lmpkg list
lmpkg install ./org.example.hello-1.0.0.lmpkg
lmpkg launch org.example.hello
lmpkg remove org.example.hello
lmpkg remove org.example.hello --purge-data
```

On-device installation/removal by the trusted `leon` desktop user goes through the broker. UID 0 may also operate locally. `--root DIR` creates an isolated offline store for tooling and tests and uses `DIR/builtin` for any built-in test manifests.
