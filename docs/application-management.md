# Application management

Settings uses the existing root application broker; it does not install a new
package daemon or change the ELF/manifest format. The broker authenticates Unix
socket peers. UID 0, desktop UID 1000 and Settings UID 11000 can use `app_list`,
`app_info`, `app_stop`, `app_clear` and `app_uninstall` on `/run/leon/apps.sock`.
Other app UIDs cannot inspect or modify another application's management data.
The existing `list`, `launch` and `install` operations remain desktop/root-only.

## Settings UI

Open **Settings → Apps** (设置 → 应用管理) from the category home, or find it
with the home search. The page lists installed builtins and local packages,
supports name/ID filtering, and opens an individual app's information page.
Details show version, honest source, current running state, declared permission
status, allocated package/private/runtime storage and supported actions. System
uninstall and Settings self-stop/reset are disabled in both UI and broker.

Stop preserves app data and grants. Clear data and uninstall first show a
confirmation naming the app and explaining deletion, permission reset and the
preservation of shared files. Cancel has no mutation side effect. A started
request continues if the user navigates away. Completed mutations refresh the
details/catalog; errors remain explicit and can be retried with Refresh.

Management requests run on a worker; controls disable during a request and idle
pages refresh every five seconds. Settings defers the built-in support layer's
synchronous preference polling during a pending request, so a stopped/busy broker
does not block back navigation through that extra poll. Other builtins retain
their existing polling behavior. The internal `ApplicationWindow::defer_preferences`
hook is not part of the installed LMUI SDK. Native fixtures can select only the
management endpoint with `LEON_APPS_SOCKET`; production uses the default socket.

## Wire protocol

Requests are newline-delimited JSON using the existing bounded broker transport.
Every response contains `ok`; errors contain `error` and must be displayed as
failures. A request's claimed UID never supplies authorization.

- `{"op":"app_list"}` returns `apps`, sorted by application ID. Each item has
  `id`, `name`, `name_en`, `version`, `builtin`, `source`, `running` and `actions`.
  `actions` contains the booleans `stop`, `clear`, `uninstall`.
- `{"op":"app_info","id":"org.leon.gallery"}` returns `app`, with those fields
  plus `permissions_available`, `permissions` and `storage`.
- `{"op":"app_stop","id":"org.leon.gallery"}` stops the app, leaving its
  package, data and grants intact. It also kills remaining resource-group members.
- `{"op":"app_clear","id":"org.leon.gallery"}` stops the app, resets its grants
  and pending portal requests, and removes its private home and runtime directory.
  The installed package is retained. A later launch creates fresh private data.
- `{"op":"app_uninstall","id":"org.example.hello"}` performs the same cleanup
  and removes every installed version. Settings always uninstalls with data
  removal. Stable UID allocation is retained for a later reinstall.

Successful mutations return `completed` (the requested operation) and `id`.
Clients refetch details/list after completion; runtime status is a snapshot.
The C++ IPC client allows 15 seconds for management mutations and legacy `remove`.

`source` is `system-image` for builtins or `local-unsigned` for installed local
packages. No publisher identity or signature verification is implied. Permissions
list only manifest declarations; entries have `name` and `state`: `allowed`,
`denied`, `pending`, `not-requested`, or `unavailable`. “Not requested” still means
no access. Portal failure is reported as unavailable, never as an inferred denial.
Apps with no declarations have an empty list.

`storage` has `package`, `data`, and `runtime`. Each has `available:true,bytes:N`
or `available:false,error:TEXT`. Bytes are allocated filesystem blocks (`st_blocks
* 512`), including directory blocks, and each inode is counted once per category.
Builtin package usage covers its executable only; shared libraries and system
resources are excluded. Image-owned executable links resolve to their real file;
application data links are never followed. Local package usage includes all retained versions.
Private data includes logs, imported files, checkpoints and temporary files.
Runtime space is reported separately. Live usage is an estimate, not a filesystem
quota or a transactional snapshot.

## Cleanup and failure behavior

System applications cannot be uninstalled. Settings cannot stop or reset itself.
Validation happens before stopping an app; a rejected system uninstall has no
process side effect. Private data removal never touches device credentials, root
policy, global Settings preferences, another app's home, or shared user files.
Legacy broker `remove` uses the same validation, stop and permission reset; its
`purge` flag still controls whether private data is retained. Offline `lmpkg
--root` remains an image/development tool, without running services to coordinate.
Settings always uses the broker; direct privileged package-library operations
remain administrative tools and do not provide that cross-service coordination.

Stopping first requests lifecycle shutdown, then escalates through SIGTERM to
SIGKILL on the existing deadlines. On target, the broker kills the entire cgroup
and waits for `populated 0` before deleting data. This includes detached children
and outstanding checkpoint writers. Development roots lack cgroup isolation and
use the existing process-group fixture behavior.

Storage walks use descriptor-relative `fstatat/openat/unlinkat`, never traverse
app-owned symlinks, reject a different filesystem, and limit depth to 48, entries
to 50,000 and elapsed walk time to one second per category/removal. Hard links
are unlinked without altering the linked file's contents. Unreadable, changing or
excessively large trees produce an explicit error rather than a false zero or a
successful clear. Parent directories are broker-owned. Production apps cannot
mount filesystems or mutate those parents under their sandbox.

The portal adds two **root-only** operations with installed `app` ID:
`app_permissions` returns the declarations and grant/pending states; `reset_app`
durably writes an empty policy for the app and invalidates its in-memory pending
requests/selections. `reset_app` never calls the broker, preventing a synchronous
broker/portal callback cycle. The broker is responsible for stopping writers.

Stop/reset/delete/uninstall are ordered operations, not a cross-service atomic
transaction. If storage or the portal fails, the operation returns an error;
the app may already be stopped, grants may already be reset, or a tree partially
removed. It is safe to retry. Portal failure prevents data deletion and package
removal. A crash during a filesystem deletion can require retrying it; no rollback
or complete power-loss recovery is claimed for the 1.0.0 graphics prototype.

## Validation

`tests/test_app_management.py` uses real peer UIDs, cooperative checkpoint writers,
private broker/portal instances and filesystem fixtures. It covers management
authorization, protected apps, permission states/reset persistence, retained
identity, all-version uninstall, links/FIFOs, walk limits and unavailable portal.
`tests/target/system.c` adds live AArch64 broker management checks, including a
SIGSTOP-stalled client and a detached writer in the app's resource cgroup.

Backend implementation validation: `make test` passed 10 CTest cases and 66
Python tests, including the six management fixtures. The rebuilt AArch64 image
passed `scripts/test-qemu.py --timeout 300 --core-only --system-probe
build/system-probe`: live management/cgroup checks, existing core service and
sandbox probes, IME crash recovery, permission denial/approval, file import and
offline private-state inspection. Build logs, probes and disk evidence remain
under ignored `build/` and `output/` paths.

Reproduce the interactive checks after building host binaries and target image:

```sh
./scripts/test-wayland.sh
python3 scripts/test-wayland-input.py --applications
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 300 --applications-only \
  --applications-probe build/applications-probe \
  --package build/hello/org.example.hello-1.0.0.lmpkg
```

The QEMU helper is injected into a disposable disk and is absent from the release
image. It observes app metadata/storage under the real console UID independently
of the Settings display. The focused run drives stop, cancel/confirm clear,
protected Settings controls, permission display, cancel/confirm uninstall, then
performs an actual reboot to check persisted reset/uninstall and builtin relaunch.

The final native checks passed 11 CTest cases (including the daemon-disconnect
transport regression) and 66 Python tests. Independent Weston passed all eight
apps, shell/shade/motion/lifecycle smoke tests and actual Settings management
input: search, details, stop, cancel/confirm clear and uninstall, permission reset,
SIGSTOP-broker back navigation, daemon exit and refresh after restart. The focused
QEMU management run passed all UI actions, actual reboot persistence, builtin
relaunch and clean compositor shutdown on the rebuilt AArch64/musl image.
The final combined QEMU suite passed management alongside authentication/root,
core service/sandbox/crash, notifications, terminal, IME/portal, Settings,
navigation/lifecycle and SDK installation checks, followed by offline private
state inspection and clean UI poweroff. Logs are `build/app-management-qemu-full.log`
and `build/app-management-qemu-reboot-passed.log`.

Applications that declare `root.exec` show **System confirmation · Root password
every time**. This is a distinct `system-confirmation` state, never an ordinary
portal allow grant. Manage per-installation request blocking, revoke active
commands and read audit events under System → Developer options → Application
Root requests. Stop cancels pending/active Root commands; clear and uninstall
wait for Root cgroup writers and reset request restrictions before changing
private files. A cleanup failure refuses the destructive action. Root helpers
can have already changed system files outside the app's storage; clearing data
cannot reverse those changes. See `root-access.md` for the protocol and limits.
