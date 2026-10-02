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
  places is for the keyboard (Meta+arrows) and Meta+drag. Known
  tension: stash and parking columns re-form when a window is dropped
  into them, which moves the dropped window; the user knows this isn't a
  perfect rule. Prefer the least movement when designing crowding.
- Modifier: Meta (Super; Command on the Mac keyboard) is the window
  system's key; Ctrl/Shift/Alt belong to apps (Alt+Tab is the universal
  exception). The user is fine being aggressive with Meta ("opinionated
  window manager"), as long as what KDE users rely on keeps working or gets
  a better replacement. Meta moves, Meta+Alt selects. Meta+mouse:
  Meta+drag moves (with acceleration and snapping), Meta+double-click
  declutters; Meta+click and Meta+wheel are free for future features.
  Meta+keyboard shortcuts other than the arrows, Meta+C and Meta+Tab are
  left to KDE.

## Features (built) and plans
| | |
|---|---|
| [docs/dragging-and-parking.md](docs/dragging-and-parking.md) | Shrink while dragging, parking, input to parked windows, icon-like tiny windows |
| [docs/stacks.md](docs/stacks.md) | Stash and parking columns |
| [docs/keyboard.md](docs/keyboard.md) | Meta+arrows ladder, Meta+Up/Down views, Meta+Alt+arrows selection |
| [docs/meta-drag.md](docs/meta-drag.md) | Meta+drag acceleration and pause to snap |
| [docs/focus-ring.md](docs/focus-ring.md) | Focus ring and bounce |
| [docs/declutter.md](docs/declutter.md) | Meta+double-click declutter and undo |
| [docs/hover-previews.md](docs/hover-previews.md) | Parking icons grow in place on hover |
| [docs/clips.md](docs/clips.md) | Text drops and Meta+C become clip windows |
| [docs/logout-hang.md](docs/logout-hang.md) | Open issue: plasmashell hangs at logout |
| [docs/wayfire.md](docs/wayfire.md) | The Wayfire prototype (reference) |
| **[plans/alt-tab.md](plans/alt-tab.md)** | **Active:** Alt+Tab hunt and return, the desktop map |
| [plans/backlog.md](plans/backlog.md) | Agreed-but-unbuilt items, polish, packaging, ideas |
| [plans/clips-back.md](plans/clips-back.md) | Getting clips back into documents |

When a plan is built, move its design into a `docs/` file and update the
table and Status.

## Status (2026-10-02)
Running in the user's real Plasma session (via use-in-session.sh; after a
rebuild the user logs out and back in). Built and working: everything in
the docs table. Last changes (2026-10-02, tested by the user, committed):
the half/full views (Meta+Up/Down as fixed views), full-width windows to
and from the stash, stash windows centred in the zone, the bounce on any
focus change, and Meta+drag snapping mode. Next: build Alt+Tab phase 1
(design and build plan in [plans/alt-tab.md](plans/alt-tab.md); all four
parts at once, agreed with the user, not started).

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
- KDE features we replace are switched off while loaded and restored on
  unload (quick tiling, some KGlobalAccel actions; see
  [docs/keyboard.md](docs/keyboard.md)).

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
- Local git identity (repo-only config): Scott Jenson <scott@jenson.org>.
- The user wants work committed and pushed so nothing is lost. Commit when a
  change is done and working; ask before pushing anything unusual.
- Ignored: `build/` (any), `kwin.log*`, `wayfire.log`. `.vscode/` is the
  user's (untracked).

## Files
- `kwin/main.cpp`: the effect (only source file). `kwin/metadata.json`:
  plugin metadata (id `glance`, from the CMake target name; shown as
  "Glance" in Desktop Effects).
- `kwin/CMakeLists.txt`: builds `kwin/build/bin/kwin/effects/plugins/glance.so`.
  Needs `find_package(ECM <version>)` (else no output folder), Qt
  Widgets/DBus/Quick (KWin's CMake target needs them), C++23, and
  `AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY`. `sudo cmake --install
  kwin/build` installs it (`kcoreaddons_add_plugin` with
  `INSTALL_NAMESPACE`; README's install steps); not installed on this VM,
  which loads it from the build folder instead.
- `kwin/kwin-private/`: KWin headers that Fedora's kwin-devel doesn't
  install but that we need (see [docs/clips.md](docs/clips.md)). Keep in
  step with the installed KWin.
- `kwin/setup-kwrite.sh`: KWrite font for clips (run once on this VM).
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
    ~/Glance/kwin/run-nested.sh        # user runs it, from Konsole in the VM window

Success check: log has `glance: effect loaded`. KWin loads effects at
startup: restart the nested session after rebuilding. Headless load check
the agent can run itself:
`XDG_RUNTIME_DIR=/run/user/1000 QT_PLUGIN_PATH=$PWD/kwin/build/bin
QT_FORCE_STDERR_LOGGING=1 kwin_wayland --virtual --socket es-check
--exit-with-session "sleep 3"`.

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
- Mac→VM clipboard (VMware Tools, `vmtoolsd -n vmusr`) is unreliable and
  adds a trailing NUL byte; keep commands for the user short or put them
  in scripts.
- Linux paths are case-sensitive.
