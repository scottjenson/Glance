# Glance: a window-management effect for KDE Plasma (KWin)

This file holds what every session needs. Details live in `docs/` (built
features: design + how it works) and `plans/` (not built yet); read the one
for the feature you're working on. Keep this file short: put feature detail
in those files and only a line here.

## Goal
A window-management experiment for wide (ultrawide) monitors: as the user
drags a window toward the left or right edge of the screen, the window
shrinks, and windows parked at the edge should feel like icons, using the
peripheral areas of the screen. Prototyped first in HTML/JavaScript on a
Mac, then as a Wayfire plugin (now in `wayfire/`, reference only).

**Goal: ship something people can try on KDE Plasma.** The product is the
KWin effect in `kwin/`. Target: desktops with ultrawide monitors, not
laptops (don't suggest testing on a laptop). The project is a demo for
exploring fairly radical ideas, not a conservative KDE add-on.

**Name:** Glance (chosen 2026-09-30: you glance at the windows on the
sides). Formerly WideMonitorUX (project/repo) and edge-shrink (the effect).
Renamed everywhere: code, scripts, docs, the project folder (~/Glance) and
the GitHub repo (scottjenson/Glance; GitHub redirects the old URL).
"Overview" was ruled out: KDE's own Meta+W effect.

## Core design rules (agreed with the user)
- Regions (user's terms, settled 2026-09-30; use them everywhere: docs,
  comments, identifiers): **main** (center half), **stash** (everything
  between main and parking; its width depends on the monitor) and
  **parking** (the very edge, icon-sized windows, ~15%). The left and right
  quarters are the edge zones (`zoneFraction = 0.25`). Formerly called
  middle, staging and parking lot. "Parked" (verb/state) = dropped while
  shrunk, in a stash or a parking area (`m_parked` holds both).
- Windows shrink as their edge enters an edge zone, down to `minScale`
  (0.15) at the screen edge; dropped while shrunk they stay where and as
  large as drawn, fully usable, and the app really resizes
  ([docs/dragging-and-parking.md](docs/dragging-and-parking.md)).
- Stashed and parked windows are meant to be used in place, not faded.
  Parking is Glance's minimize.
- **Respect mouse drags** (user's principle, 2026-09-30): a plain mouse
  drop (a window, or a clip dropped on the desktop) should land where and
  as large as the user put it, changed as little as possible. Snapping to
  places is for the keyboard (Meta+arrows) and Meta+drag. Stashes have
  no column (drops stay put, overlap is fine); parking columns still
  re-form when a window is dropped into them, which moves it. Prefer the
  least movement when designing crowding.
- Modifier: Meta (Super; Command on the Mac keyboard) is the window
  system's key; Ctrl/Shift/Alt belong to apps (Alt+Tab is the universal
  exception). The user is fine being aggressive with Meta ("opinionated
  window manager"), as long as what KDE users rely on keeps working or gets
  a better replacement. Meta moves, Meta+Alt selects. Meta+mouse:
  Meta+drag moves (with acceleration and snapping), Meta+double-click
  declutters, Meta+wheel resizes in place; Meta+click is free for future
  features.
  Meta+keyboard shortcuts other than the arrows, Meta+C and Meta+Tab are
  left to KDE.

## Features (built) and plans
| | |
|---|---|
| [docs/dragging-and-parking.md](docs/dragging-and-parking.md) | Shrink while dragging, parking, input to parked windows, icon-like tiny windows, minimize = park |
| [docs/stacks.md](docs/stacks.md) | Parking columns; stashes are free placement |
| [docs/meta-wheel.md](docs/meta-wheel.md) | Meta+wheel resizes in place; over parking icons it sizes the preview |
| [docs/keyboard.md](docs/keyboard.md) | Meta+arrows ladder, Meta+Up/Down views, Meta+Alt+arrows selection, Meta alone and KDE's launcher |
| [docs/meta-drag.md](docs/meta-drag.md) | Meta+drag acceleration, pause to snap, activates the window |
| [docs/focus-ring.md](docs/focus-ring.md) | Focus ring; bounce on keyboard focus moves only |
| [docs/declutter.md](docs/declutter.md) | Meta+double-click declutter and undo |
| [docs/hover-previews.md](docs/hover-previews.md) | Parking icons grow in place on hover |
| [docs/clips.md](docs/clips.md) | Text and image drops and Meta+C become clip windows (glance-clip sticky notes) that drag back into documents |
| [docs/alt-tab.md](docs/alt-tab.md) | Alt+Tab hunt and return, the desktop map (phase 1; phase 2 listed there) |
| [docs/logout-hang.md](docs/logout-hang.md) | Plasmashell hangs at logout: a KDE clipboard deadlock (root cause known, harmless, left alone) |
| [docs/wayfire.md](docs/wayfire.md) | The Wayfire prototype (reference) |
| [plans/backlog.md](plans/backlog.md) | Agreed-but-unbuilt items, polish, packaging, ideas |
| [plans/clips-back.md](plans/clips-back.md) | Clips phase 2: rich text |
| [plans/code-review.md](plans/code-review.md) | Architecture/performance review (2026-10-03): 5 fixes to make, what to keep; read before touching painting, input routing or clips |
| [plans/refactor.md](plans/refactor.md) | Splitting main.cpp into components (review finding 5), in stages; the smoke test to run after each |

When a plan is built, move its design into a `docs/` file and update the
table and Status.

## Status (2026-10-03, end of day)
Running in the user's real Plasma session (via use-in-session.sh; after a
rebuild the user logs out and back in). Built, tested by the user and
committed: everything in the docs table. Latest (all 2026-10-03): a
stashed window keeps its size when dragged; Meta+wheel over a parking
icon sizes its hover preview; the bounce only on keyboard focus moves;
KDE's launcher only on a real Meta tap (the user keeps an eye on it);
Meta+drag activates and raises; image clips (Firefox drops tested by the
user; Dolphin file drops and dragging an image clip into apps not yet
confirmed). README.md is up to date.

Next: not decided; ask the user. Suggested: the fixes in
[plans/code-review.md](plans/code-review.md); crowding of parking columns
(agreed, needs design talk first); Meta+C with a clipboard image; stash follow-ups (should small stashed windows stop
acting like icons? Meta+wheel speed/direction by feel). Other
candidates: Alt+Tab phase 2 (click in the map etc.,
[docs/alt-tab.md](docs/alt-tab.md)); declutter follow-ups; rich-text
clips; open clip questions (clips aren't restored after login; Alt+Tab
shows clips with the icon only).

## How the effect works (kwin/main.cpp, the basics)
- It is a KWin **effect**, not a plain `KWin::Plugin`, so `prePaintWindow`
  can call `data.setTransformed()` for scaled windows. Without that, KWin
  clips drawing in unscaled coordinates (`clipQuads` in
  scene/itemrenderer_opengl.cpp uses only the translation), so only the
  top-left part of a shrunk window is painted, and occlusion treats it as
  still covering its full-size area. `isActive()` is true only while
  something is scaled (or bouncing).
- Drawing: a `QTransform` on the window's `WindowItem` (item coordinates
  start at the frame's top-left corner), always through `setDrawTransform`.
  A window has a real frame (what the app and KWin's input know) and a
  drawn position/size; most of Glance is about keeping the two in step.
- Input: an `InputEventFilter` at `InputFilterOrder::ButtonRebind` (very
  early, before KWin's own shortcuts, tab box and DnD). `Effect` has its own
  `pointerMotion` etc. virtuals, so the filter is a separate member object
  (`Filter`) that calls back.
- KDE features we replace or change are switched while loaded and
  restored on unload (quick tiling off, some KGlobalAccel actions off,
  [docs/keyboard.md](docs/keyboard.md); Meta+left-drag's mouse command
  "Move" becomes "Activate, Raise and Move", [docs/meta-drag.md](docs/meta-drag.md)).

## Environment
- Fedora 44 KDE (aarch64) in a VMware Fusion VM on an Apple Silicon Mac,
  VMware SVGA3D virtual GPU. The user has VM snapshots to roll back to.
- User: scottjenson. Project at ~/Glance (was ~/WideMonitorUX). The user edits in VS Code,
  connected into the VM.
- KWin **6.7.5** (Plasma 6.7). Its source is unpacked at ~/src/kwin-6.7.5
  for checking internals (`dnf download --source kwin`, then
  `rpm2cpio kwin-*.src.rpm | cpio -idm` and untar).
- Build packages (installed): `kwin-devel`, `extra-cmake-modules`,
  `libepoxy-devel` (kwin-devel doesn't pull it in),
  `kf6-kglobalaccel-devel` (for the Meta+C shortcut; installed 2026-10-01).
- Display: VMware "Use full resolution for Retina display" is on; the VM
  screen is about 6000x2450 px (it follows the VM window size). KDE scale
  changes: 200% earlier, **150%** since 2026-09-29 evening (4004x1630
  logical). Check with `kscreen-doctor -o` (desktop env, see below).
- One desktop (no virtual desktops in use).

## Git / GitHub
- Repo: https://github.com/scottjenson/Glance (**public**; renamed from
  WideMonitorUX on 2026-09-30), branch
  `main`, remote `origin` over HTTPS. `gh` is logged in and is the git
  credential helper, so `git push` works without prompts.
  If a push says the token is invalid (happened twice on 2026-10-03,
  suspected: the keyring loses it at logout), the user runs
  `gh auth login -h github.com -p https -w --insecure-storage` (the agent
  may not; give it as a tiny script, the VM clipboard is broken). It
  shows an 8-character code: the user types it at
  github.com/login/device on the Mac and clicks Authorize (no
  checkboxes; a hand-made token needs repo, read:org and workflow). On
  2026-10-03 it still landed in the keyring: watch after the next logout.
- Local git identity (repo-only config): Scott Jenson <scott@jenson.org>.
- The user wants work committed and pushed so nothing is lost. Commit when a
  change is done and working; ask before pushing anything unusual.
- Ignored: `build/` (any), `kwin.log*`, `wayfire.log`. `.vscode/` is the
  user's (untracked).

## Files
- `kwin/main.cpp`: the effect (being split up, [plans/refactor.md](plans/refactor.md)).
  `kwin/tuning.h`: all tuning constants and the Place enum (namespace
  `glance`). `kwin/geometry.h/.cpp`: pure geometry (no KWin), built as
  the `glance-core` library; `kwin/tests/`: its Qt Test unit tests.
  `kwin/parked.h/.cpp`: ParkedWindows, the parked-window model (places,
  animation, making room, draw transforms, and window queries such as
  `pick`); features use it through `m_parking`. Feature components
  (each gets references to the parts it needs): `clips.h/.cpp`,
  `alttab.h/.cpp`, `declutter.h/.cpp`, `previews.h/.cpp` (hover
  previews), `wheel.h/.cpp` (Meta+wheel). Still in main.cpp: dragging,
  keyboard, icon presses, focus ring, input routing (refactor stage 4).
  `kwin/clip/main.cpp`: glance-clip, the
  clip app (a second target in the same CMake project, built to
  `kwin/build/bin/glance-clip`; [docs/clips.md](docs/clips.md)). `kwin/metadata.json`:
  plugin metadata (id `glance`, from the CMake target name; shown as
  "Glance" in Desktop Effects).
- `kwin/CMakeLists.txt`: builds `kwin/build/bin/kwin/effects/plugins/glance.so`.
  Needs `find_package(ECM <version>)` (else no output folder), Qt
  Widgets/DBus/Quick (KWin's CMake target needs them), Concurrent (clips save off the main thread), C++23, and
  `AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY`. `sudo cmake --install
  kwin/build` installs it (`kcoreaddons_add_plugin` with
  `INSTALL_NAMESPACE`; README's install steps); not installed on this VM,
  which loads it from the build folder instead.
- `kwin/kwin-private/`: KWin headers that Fedora's kwin-devel doesn't
  install but that we need (see [docs/clips.md](docs/clips.md)). Keep in
  step with the installed KWin.
- `kwin/setup-kwrite.sh`: KWrite font for the old KWrite clips (unused).
- `kwin/run-nested.sh`: starts a nested KWin (a window in the desktop,
  2982x1090 logical at scale 2, override with WIDTH/HEIGHT) with
  QT_PLUGIN_PATH at the build folder and a Konsole inside. Must be run from
  Konsole in the VM window (not SSH). Log: `~/Glance/kwin.log`,
  previous one `kwin.log.1`.
- `kwin/use-in-session.sh on|off`: loads the effect into the real Plasma
  session from kwin/build (a systemd drop-in,
  ~/.config/systemd/user/plasma-kwin_wayland.service.d/glance.conf (the old
  edge-shrink.conf is removed by the script),
  setting QT_PLUGIN_PATH for KWin only); takes effect at the next login.
  **Currently on.** The user runs it (auto mode blocks the agent from
  changing what loads at login). After a rebuild: log out and back in.
  In the real session, KWin's log is in the journal:
  `journalctl --user -b -o cat | grep glance:`.
- `kwin/nested-firefox.sh`: run inside the nested session; opens
  `test/breakpoints.html` in a separate Firefox (`--no-remote`, own profile),
  since plain `firefox` would open in the desktop's instance.
- `test/breakpoints.html`: color/label change at widths 1200/800/600/500 px,
  shows its inner size.
- `wayfire/`: the Wayfire 0.10.1 prototype ([docs/wayfire.md](docs/wayfire.md)).
- `docs/`, `plans/`: see the table above.

## Build and run
    cmake -S kwin -B kwin/build        # once
    cmake --build kwin/build
    ctest --test-dir kwin/build        # unit tests, no KWin needed
    ~/Glance/kwin/run-nested.sh        # user runs it, from Konsole in the VM window

Success check: log has `glance: effect loaded`. KWin loads effects at
startup: restart the nested session after rebuilding. Headless load check
the agent can run itself:
`XDG_RUNTIME_DIR=/run/user/1000 QT_PLUGIN_PATH=$PWD/kwin/build/bin
QT_FORCE_STDERR_LOGGING=1 kwin_wayland --virtual --socket es-check
--exit-with-session "sleep 3"`.

Headless screenshot loop (the agent can check drawing itself, no logout):
a virtual KWin on its own D-Bus, with windows, `GLANCE_TEST_MAP=1` (opens
the Alt+Tab map 3 s after loading) and Spectacle; then measure the PNG:
`GLANCE_TEST_MAP=1 XDG_RUNTIME_DIR=/run/user/1000
QT_PLUGIN_PATH=$PWD/kwin/build/bin dbus-run-session -- kwin_wayland
--virtual --width 4004 --height 1630 --scale 1.5 --socket glance-test
--exit-with-session "sh -c 'konsole -qwindowgeometry 3900x1500+50+50 &
sleep 3.6; spectacle -b -n -f -o <file>.png'"`. Use big windows: small
ones near the centre hide clipping bugs.
More tricks for that session (used 2026-10-03; put the steps in a script
run by `--exit-with-session "sh script.sh"`):
- KWin scripts over the session's private D-Bus: write a .js file, then
  `id=$(qdbus-qt6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript
  file.js name)` and `qdbus-qt6 org.kde.KWin /Scripting/Script$id
  org.kde.kwin.Script.run`. E.g. `w.minimized = true` parks windows
  (minimize = park), `console.info(...)` prints window info (resourceClass,
  desktopFileName, frameGeometry) to the log. Works on the real session
  too (with its DBUS_SESSION_BUS_ADDRESS), read-only scripts only.
- glance-clip needs `QT_QPA_PLATFORMTHEME=kde` there for its yellow title
  bar (the real session has it).
- Input can't be injected (no fake-input tool): drags, hover and keys
  need the user.
Crash stacks: `DEBUGINFOD_URLS=https://debuginfod.fedoraproject.org/
coredumpctl debug <pid> --debugger-arguments="-batch -ex 'thread apply all
bt 20'"` (symbols download on demand; `coredumpctl list` first).

## Notes for the agent
- The user is new to Linux/Wayland development: explain Linux-specific steps
  (sudo, dnf, -devel packages, etc.) and give exact commands.
- The user prefers simple options (e.g. HTTPS over SSH) and likes design
  choices talked through before big changes.
- The user runs the nested session and reports how it feels; the agent can
  build but can't interact with it. It *can* see it: screenshot the whole
  VM desktop with `spectacle -b -n -f -o <file>` using the desktop's env
  (DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000
  DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus), and query the
  nested screen with `WAYLAND_DISPLAY=wayland-1 wayland-info`. `sudo` needs
  the user's password, so the user runs installs.
- On the Mac keyboard in the VM: Option = Alt, Command = Meta/Super.
  VMware Fusion's Mac shortcut mappings turned Command+click into
  Ctrl+click (Firefox opened new tabs, KDE's Meta+drag did nothing); the
  user turned that mapping off in Fusion's Keyboard & Mouse settings
  (2026-09-30). If Meta+click stops working, check there first. Same for
  keys: Fusion's Key Mappings turned Command+C into Ctrl+C, so Meta+C never
  reached KWin; the user turned that mapping off (2026-10-01) and copies
  with Ctrl+C in the VM. If a Meta+letter shortcut does nothing (no
  `glance:` log line), check Fusion's Key Mappings. Command+Tab may be
  taken by macOS itself before the VM sees it; check when building Meta+Tab.
  Fusion sometimes holds Command back (2026-10-03, seen in a key log): the
  Meta press reaches KWin only at release, as an instant press+release,
  unless a click comes first. Then Meta+wheel arrives as a plain scroll
  (Meta+drag still works, the click lets Command through). Workaround:
  Command+click on the empty desktop once; it works again. Trigger not
  pinned down (around switching to the Mac / making a clip).
  Those instant taps also opened KDE's launcher at random; Glance now
  ignores taps under 10 ms ([docs/keyboard.md](docs/keyboard.md)).
- plasma-keyboard (KDE's on-screen keyboard, started by KWin) sometimes
  crashes at login inside Mesa's VMware `svga` driver (context creation;
  checked 2026-10-03 with `coredumpctl debug`): a VM GPU issue, not Glance.
  KWin restarts it. Off switch: System Settings → Keyboard → Virtual
  Keyboard → None. It comes from the VM's GPU state wearing down after
  many logouts without a reboot (~20 over 4-5 days, 2026-10-03); then
  Xwayland and ksplashqml crash the same way at login, KWin gives up on
  Xwayland, and the Mac→VM clipboard dies (vmtoolsd's helper is X11).
  Fix: reboot the VM. The crashes started before glance-clip ever ran.
- Mac→VM clipboard (VMware Tools, `vmtoolsd -n vmusr`) is unreliable and
  adds a trailing NUL byte; keep commands for the user short or put them
  in scripts.
- Linux paths are case-sensitive.
