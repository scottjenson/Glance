# Glance: a window-management effect for KDE Plasma (KWin)

This file holds what every session needs. Details live in `docs/` (built
features: design and how it works) and `plans/` (not built yet); read the
one for the feature you're working on. Keep this file short, and keep the
docs about the feature as it is now, not the history of how it got there
(git has that).

## Goal
A window-management experiment for wide (ultrawide) monitors: as the user
drags a window toward the left or right edge of the screen, the window
shrinks, and windows parked at the edge feel like icons, using the
peripheral areas of the screen. Prototyped in HTML/JavaScript, then as a
Wayfire plugin (`wayfire/`, reference only).

**Goal: ship something people can try on KDE Plasma.** The product is the
KWin effect in `kwin/`. Target: desktops with ultrawide monitors, not
laptops (don't suggest testing on a laptop). A demo for exploring fairly
radical ideas, not a conservative KDE add-on.

**Name:** Glance (you glance at the windows on the sides). Formerly
WideMonitorUX (repo) and edge-shrink (the effect, still the name in
`wayfire/`). "Overview" is KDE's own Meta+W effect.

## Core design rules (agreed with the user)
- Regions (use these terms everywhere: docs, comments, identifiers):
  **main** (center half), **stash** (between main and parking; its width
  depends on the monitor) and **parking** (the very edge, icon-sized
  windows). The left and right quarters are the edge zones
  (`zoneFraction = 0.25`). "Parked" (verb/state) = dropped while shrunk,
  in a stash or parking (`m_parked` holds both).
- Windows shrink as their edge enters an edge zone, down to `minScale`
  (0.15) at the screen edge; dropped while shrunk they stay where and as
  large as drawn, fully usable
  ([docs/dragging-and-parking.md](docs/dragging-and-parking.md)).
- Stashed and parked windows are used in place, not faded. Parking is
  Glance's minimize.
- **Respect mouse drags:** a plain mouse drop (a window, or a clip
  dropped on the desktop) lands where and as large as the user put it,
  changed as little as possible. Snapping to places is for the keyboard
  (Meta+arrows) and Meta+drag. Stashes are free placement (overlap is
  fine); parking columns re-form when a window is dropped into them.
  Prefer the least movement when designing crowding.
- Modifier: Meta (Super; Command on the Mac keyboard) is the window
  system's key; Ctrl/Shift/Alt belong to apps (Alt+Tab is the exception).
  Being aggressive with Meta is fine ("opinionated window manager"), as
  long as what KDE users rely on keeps working or gets a better
  replacement. Meta moves, Meta+Alt selects. Meta+drag moves (with
  acceleration and snapping), Meta+double-click declutters, Meta+wheel
  resizes in place; Meta+click is free for future features. Shaking any
  drag scatters the other windows to the stashes. Meta+keyboard
  shortcuts other than the arrows, Meta+C and Meta+Tab are left to KDE.

## Features (built) and plans
| | |
|---|---|
| [docs/dragging-and-parking.md](docs/dragging-and-parking.md) | Shrink while dragging, parking, parking columns and free stashes, parking tiles, mipmaps, input to parked windows, icons, minimize = park, logging out |
| [docs/meta-wheel.md](docs/meta-wheel.md) | Meta+wheel resizes in place; over parking icons it sizes the preview |
| [docs/keyboard.md](docs/keyboard.md) | Meta+arrows, Meta+Up/Down views, Meta+Alt+arrows selection, the Meta tap and KDE's launcher |
| [docs/meta-drag.md](docs/meta-drag.md) | Meta+drag acceleration, pause to snap |
| [docs/focus-ring.md](docs/focus-ring.md) | Focus ring; bounce on keyboard focus moves |
| [docs/declutter.md](docs/declutter.md) | Meta+double-click declutter and undo; shaking a dragged window scatters the others |
| [docs/hover-previews.md](docs/hover-previews.md) | Parking icons grow in place on hover |
| [docs/clips.md](docs/clips.md) | Text/image drops and Meta+C become clip windows (glance-clip) that drag back into documents |
| [docs/alt-tab.md](docs/alt-tab.md) | Alt+Tab hunt and return, the desktop map |
| [docs/version-check.md](docs/version-check.md) | Login dialog when KWin didn't load Glance; build stops on another Plasma release |
| [docs/testing.md](docs/testing.md) | Headless checks the agent can run, KWin scripts, crash stacks, the user's smoke test |
| [plans/remembered-places.md](plans/remembered-places.md) | Next: windows reopen where they were, per app |
| [plans/backlog.md](plans/backlog.md) | Later, polish, packaging, ideas |

When a plan is built, move its design into a `docs/` file, delete the
plan, and update the table and Status.

## Status (2026-10-07)
Everything in the docs table is built, committed and checked by the user
in the real session, except two things waiting for the user's check:
parked windows staying drawn in place at logout
([docs/dragging-and-parking.md](docs/dragging-and-parking.md), Logging
out), and the drag shake ([docs/declutter.md](docs/declutter.md)).

Next: remembered places ([plans/remembered-places.md](plans/remembered-places.md));
first open question there: keep or remove the logout step.

## How the effect works (the basics)
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
- KDE features Glance replaces are switched off while loaded and restored
  on unload, in memory only (quick tiling, some KGlobalAccel actions,
  [docs/keyboard.md](docs/keyboard.md); Meta+left-drag becomes "Activate,
  Raise and Move", [docs/meta-drag.md](docs/meta-drag.md)).

## Files
- `kwin/glance.cpp`: the effect, a coordinator: owns the components,
  hands them window events and painting, and decides in one place which
  gets an input event first (its header comment maps the components).
- `kwin/tuning.h`: all tuning constants and the Place enum (namespace
  `glance`). `kwin/geometry.h/.cpp`: pure geometry (no KWin), built as the
  `glance-core` library; `kwin/tests/`: its Qt Test unit tests.
- `kwin/parked.h/.cpp`: ParkedWindows, the parked-window model (places,
  animation, making room, draw transforms, tilt, queries such as `pick`,
  minimize = park); features use it through `m_parking`.
- Components (each gets references to the parts it needs):
  `parkedinput` (input to parked windows, icon presses), `drag`,
  `keyboard`, `alttab`, `focusring`, `previews` (hover previews), `wheel`
  (Meta+wheel), `declutter`, `clips`, `mipmaps`, `kde` (KDE settings
  switched while loaded, the Meta tap). Don't name a file input.h: it
  would shadow KWin's `<input.h>`.
- `kwin/clip/main.cpp`: glance-clip, the clip app (a second target, built
  to `kwin/build/bin/glance-clip`; [docs/clips.md](docs/clips.md)).
- `kwin/check/`: glance-check, the login dialog, and its autostart entry
  ([docs/version-check.md](docs/version-check.md)).
- `kwin/metadata.json`: plugin metadata (id `glance`, from the CMake
  target name; "Glance" in Desktop Effects).
- `kwin/CMakeLists.txt`: builds `kwin/build/bin/kwin/effects/plugins/glance.so`.
  Needs `find_package(ECM <version>)` (else no output folder), Qt
  Widgets/DBus/Quick (KWin's CMake target needs them), Concurrent, C++23,
  and `AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY`. `sudo cmake --install
  kwin/build` installs it (README); not installed on this VM, which loads
  it from the build folder.
- `kwin/kwin-private/`: KWin headers Fedora's kwin-devel doesn't install
  but Glance needs. Keep in step with the installed KWin
  (`GLANCE_KWIN_PRIVATE_VERSION`, [docs/version-check.md](docs/version-check.md)).
- `kwin/use-in-session.sh on|off`: loads the effect into the real Plasma
  session from kwin/build (a systemd drop-in,
  ~/.config/systemd/user/plasma-kwin_wayland.service.d/glance.conf,
  setting QT_PLUGIN_PATH for KWin only, plus glance-check's autostart
  entry in ~/.config/autostart); takes effect at the next login.
  **Currently on.** The user runs it (auto mode blocks the agent from
  changing what loads at login). After a rebuild: log out and back in.
  KWin's log is in the journal: `journalctl --user -b -o cat | grep glance:`.
- `kwin/run-nested.sh`: a nested KWin (a window in the desktop, 2982x1090
  logical at scale 2, override with WIDTH/HEIGHT) with a Konsole inside.
  Run from Konsole in the VM window (not SSH). Log: `~/Glance/kwin.log`.
  `kwin/nested-firefox.sh` opens `test/breakpoints.html` (shows its inner
  size, changes at 1200/800/600/500 px) in a separate Firefox there.
- `wayfire/`: the Wayfire 0.10.1 prototype, reference only (build:
  `meson setup wayfire/build && meson compile -C wayfire/build`).

## Build and run
    cmake -S kwin -B kwin/build        # once
    cmake --build kwin/build
    ctest --test-dir kwin/build        # unit tests, no KWin needed

Success check: the log has `glance: effect loaded`. KWin loads effects at
startup, so the user logs out and in after a rebuild. The agent can check
loading and drawing itself in a headless KWin
([docs/testing.md](docs/testing.md)); input can't be injected, so drags,
hover and keys need the user.

## Environment
- Fedora 44 KDE (aarch64) in a VMware Fusion VM on an Apple Silicon Mac,
  VMware SVGA3D virtual GPU. The user has VM snapshots to roll back to.
- User: scottjenson. Project at ~/Glance. The user edits in VS Code,
  connected into the VM.
- KWin **6.7.5** (Plasma 6.7). Source unpacked at ~/src/kwin-6.7.5 for
  checking internals (`dnf download --source kwin`, `rpm2cpio ... | cpio
  -idm`, untar).
- Build packages (installed): `kwin-devel`, `extra-cmake-modules`,
  `libepoxy-devel`, `kf6-kglobalaccel-devel`.
- Display: about 6000x2450 px (VMware "full resolution for Retina"; it
  follows the VM window size), KDE scale **150%** (4004x1630 logical).
  Check with `kscreen-doctor -o` (desktop env, below). One desktop.

## Git / GitHub
- Repo: https://github.com/scottjenson/Glance (**public**), branch `main`,
  remote `origin` over HTTPS; `gh` is the git credential helper. Local
  identity: Scott Jenson <scott@jenson.org>.
- If a push says the token is invalid (happens now and then, cause
  unknown), the user runs `~/gh-login.sh` (the agent may not): it shows
  an 8-character code to type at github.com/login/device on the Mac. Since
  2026-10-04 the token is stored in ~/.config/gh/hosts.yml.
- Commit when a change is done and working, and push so nothing is lost;
  ask before pushing anything unusual.
- Ignored: `build/`, `kwin.log*`, `wayfire.log`. `.vscode/` is the user's.

## Notes for the agent
- The user is new to Linux/Wayland development: explain Linux-specific
  steps (sudo, dnf, -devel packages) and give exact commands. `sudo`
  needs the user's password, so the user runs installs.
- The user prefers simple options and likes design choices talked
  through before big changes.
- Seeing the desktop: `spectacle -b -n -f -o <file>` with the desktop's
  env (DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000
  DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus).
- Mac keyboard in the VM: Option = Alt, Command = Meta. Fusion's
  "Mac shortcut" mappings are turned off (they turned Command+click into
  Ctrl+click and Command+C into Ctrl+C); if a Meta shortcut does nothing
  (no `glance:` log line), check Fusion's Keyboard & Mouse settings first.
  macOS takes Command+Tab itself. Fusion sometimes holds Command back,
  sending it as an instant press+release; then Meta+wheel arrives as a
  plain scroll. Workaround: Command+click the empty desktop once.
- The Mac→VM clipboard is unreliable and adds a trailing NUL byte: keep
  commands for the user short or put them in scripts.
- VM GPU wear: after many logouts without a reboot, plasma-keyboard,
  Xwayland and ksplashqml crash at login in Mesa's `svga` driver and the
  Mac→VM clipboard dies. Not Glance. Fix: reboot the VM.
- Plasmashell sometimes hangs ~40 s at logout and abrt reports a crash:
  a deadlock in KDE's kguiaddons (`WaylandClipboard::~WaylandClipboard`,
  the clipboard changing while plasmashell quits). Not Glance, harmless,
  left alone.
- Linux paths are case-sensitive.
