# Terminal

The terminal is an LMUI Wayland client running a real interactive `/bin/sh -i`
on a controlling pseudo-terminal. Commands, pipelines, redirection, terminal
applications and shell job control execute inside the terminal application's
existing UID, Landlock, seccomp and cgroup restrictions. The starting directory
is its private `HOME`, `/data/leon/data/org.leon.terminal` on the reference image.
It is an application shell with the permissions described in
[application-format.md](application-format.md#kernel-enforcement-at-launch).

Root starts disabled. Enable it and create a password through Settings → System
→ Developer options, then use `su`, `su -` or `su -c 'COMMAND'`. Every session asks
for that password without echo. `rootd` creates a separate root PTY, with a clean
environment and its own cgroup; normal terminal commands retain the application
sandbox. Disabling root or changing its password revokes existing sessions.
`sudo` and `sudoedit` remain disabled. See [root-access.md](root-access.md).

## Display and terminal protocol

libvterm owns the incremental UTF-8/VT parser, live screen, alternate screen,
cursor, modes and terminal replies. The target uses the existing checksum-pinned
libvterm 0.3.3 package. Native builds use the host's libvterm development package;
older versions expose fewer attributes, such as concealed text and history erase.

Supported common ANSI/VT operations include cursor addressing and movement,
erase/insert/delete, scrolling regions, saved cursor, tabs, autowrap, DEC line
drawing, alternate screen, 16/256/RGB colors, bold, italic, underline, strike,
reverse video, blinking text/cursor, cursor shape/visibility and title changes.
Device/status queries, application cursor/keypad modes, function keys, modified
keys, bracketed paste, focus reporting and libvterm's mouse protocols are handled
through the same parser. This follows libvterm's supported protocol subset;
it does not promise every xterm extension. SGR faint, sixel/Kitty images,
clickable OSC 8 links and OSC 52 clipboard access are not implemented.

The image includes DejaVu Sans Mono and `xterm-256color` terminfo. The shell sets
`TERM=xterm-256color`, `COLORTERM=truecolor` and `LANG=C.UTF-8`. Cairo/Pango draws
each character cluster at fixed grid coordinates, with font ligatures disabled;
CJK and other wide characters occupy two cells and combining marks stay in
their character's cell. Colored blank cells retain their background, which is
needed by full-screen applications. Changing the font or opening the keyboard
updates both the emulator grid and the PTY size with `TIOCSWINSZ`/`SIGWINCH`.

## Controls

Type commands directly with a physical keyboard, or tap the terminal/Keys button
to show the system keyboard. The toolbar exposes Esc, Tab, Ctrl, Alt and the
four arrows. Ctrl and Alt latch for the next key, allowing Ctrl-C, Ctrl-D,
Ctrl-Z and other shell controls from the mobile keyboard.

| Control | Action |
| --- | --- |
| Ctrl-Shift-C / Copy | Copy the selected text to the Wayland clipboard |
| Ctrl-Shift-V / Paste | Paste clipboard text into the PTY |
| Select, then drag across the grid | Select text, including wide/combining characters |
| Drag vertically, mouse wheel, Shift-PageUp/Shift-PageDown | Browse scrollback |
| Ctrl-plus / Ctrl-minus, A+ / A− | Adjust the font between 10 and 24 pixels |
| Shift with mouse input | Select/scroll locally when a terminal program requests mouse events |
| Restart | Start a new shell after the previous shell exits |

Typing returns to the live screen. Scrollback is bounded by both 2,000 lines
and 262,144 cells; alternate-screen output does not enter history. Existing
history keeps its recorded cell layout when the width changes rather than
reflowing paragraphs. Clipboard paste strips control bytes except tabs and
newlines, normalizes line endings and respects bracketed-paste mode. Clipboard
ownership and size/time limits follow the [LMUI interface](interfaces.md).

The reference BusyBox image provides `vi`, `top`, `less` and `stty`. For example:

```sh
uname -a
printf '\033[38;2;120;210;255mLeonMobile\033[0m\n'
vi note.txt
top
```

Backgrounding the app retains its PTY and foreground program. Closing or
reclaiming it ends the live session; shell processes and terminal contents are
not restored after application exit. Files saved in the private home persist.
An exited shell leaves its output visible, displays its exit status and can be
restarted without relaunching the app.

## Implementation and validation

`src/apps/terminal/` separates VT state/history (`screen`), nonblocking PTY and
child lifetime (`session`), physical key translation (`input`), fixed-cell
drawing (`renderer`), event coordination (`application`) and mobile controls
(`view`). PTY input queues are capped at 1 MiB; read/write work is bounded per
dispatch and writable polling is enabled only while queued input remains.
Child processes close inherited application/Wayland descriptors before exec.

```sh
make test
./scripts/test-wayland.sh
python3 scripts/test-wayland-input.py
python3 scripts/test-wayland-input.py --terminal
./scripts/build-target-probes.sh
python3 scripts/test-qemu.py --timeout 240 \
  --system-probe build/system-probe \
  --notification-probe build/notifications-probe \
  --terminal-probe build/terminal-probe \
  --package build/hello/org.example.hello-1.0.0.lmpkg
```

The terminal probe tests VT state, Unicode, mode replies and real PTYs, including
a hash-checked 512 KiB transfer under backpressure, queue bounds, controlling
session, inherited descriptor cleanup, resizing, shell output and Ctrl-C job
control. The independent Weston input test drives the application with actual
keys and selection/clipboard events. The QEMU option runs the same probe under
the real sandboxed AArch64 terminal identity, drives RGB output, `vi`, `top`,
interruption, selection/paste and session restart, then checks exact file
contents offline after clean shutdown. Evidence stays under ignored `build/`.
