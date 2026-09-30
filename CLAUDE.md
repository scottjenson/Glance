# Glance: a window-management effect for KDE Plasma (KWin)

## Goal
A window-management experiment for wide (ultrawide) monitors: as the user
drags a window toward the left or right edge of the screen, the window
shrinks, and windows parked at the edge should feel like icons, using the
peripheral areas of the screen. Prototyped first in HTML/JavaScript on a
Mac, then as a Wayfire plugin (now in `wayfire/`, reference only).

**Goal: ship something people can try on KDE Plasma.** The product is the
KWin effect in `kwin/`.

**Name:** Glance (chosen 2026-09-30: you glance at the windows on the
sides). Formerly WideMonitorUX (project/repo) and edge-shrink (the effect).
Renamed everywhere: code, scripts, docs, the project folder (~/Glance) and
the GitHub repo (scottjenson/Glance; GitHub redirects the old URL).
"Overview" was ruled out: KDE's own Meta+W effect.

## Design rules (agreed with the user)
- Screen: main (the middle half) is full size; the left and right quarters are
  edge zones (`zoneFraction = 0.25`, relative to the screen width).
- A window is full size as long as it lies entirely within main.
  Once its left or right (drawn) edge enters an edge zone, it shrinks,
  scaled around the cursor (the grabbed spot stays under it), linearly with
  that edge's depth into the zone, reaching `minScale` (0.15) exactly when
  the edge meets the screen edge.
- Dropped while shrunk: it stays exactly where and as large as drawn
  ("parked"), stays fully usable, and the app is really resized to a
  phone-like width so web pages reflow. Dropped in main (scale
  ≥ 0.99): back to its original size.
- Scales are always relative to the window's original size.
- Regions (user's terms, settled 2026-09-30; use them everywhere: docs,
  comments, identifiers): **main** (center half), **stash** (everything
  between main and parking; its width depends on the monitor) and
  **parking** (the very edge, icon-sized windows, ~15%). Formerly called
  middle, staging and parking lot. "Parked" (verb/state) = dropped while
  shrunk, in a stash or a parking area (`m_parked` holds both).
- Modifier: Meta (Super; Command on the Mac keyboard) is the window
  system's key; Ctrl/Shift/Alt belong to apps. The user is fine being
  aggressive with Meta ("opinionated window manager"), as long as what KDE
  users rely on keeps working or gets a better replacement. Meta+mouse:
  Meta+drag stays move; Meta+click, Meta+wheel, Meta+double-click are free
  for future features. Meta+keyboard shortcuts other than the arrows are
  left to KDE.
- **Keyboard, phase 1** (agreed 2026-09-29, may evolve; replaces KDE's
  quick tiling on Meta+arrows, intercepted by our input filter, active
  window only):
  - Meta+Left from a free window in main: snap to the left half of main
    (x = main's left edge, width = half of main; height and vertical
    position unchanged; real resize). Again: 50% size in the left stash.
    Again: left parking (15%). Meta+Right mirrors and walks back
    (parking L → stash L → left half → right half → stash R → parking R).
  - Meta+Up: fill the full screen height (keep width and x). Meta+Down:
    undo Meta+Up for now (meaning still open).
  - Keyboard moves animate (180 ms, ease-out); the app resizes during the
    glide. A flash from the app re-laying out remains (noted by the user
    as spoiling the effect a bit); fix if wanted: snapshot + cross-fade
    (KWin's CrossFadeEffect, as KDE's maximize animation does).
  - KDE's Meta-tap launcher: our filter must NOT swallow Meta+arrows (KDE's
    shortcut system then thinks Meta was tapped alone and opens the
    launcher). Instead the keys are passed on and KWin's four "Window
    Quick Tile Left/Right/Top/Bottom" QActions (children of Workspace) are
    disabled while the effect is loaded. `cancelModiferOnlySequence` is
    not exported to plugins.
- **Stacks (built 2026-09-29):** each stash and parking area holds its
  windows as one column, centred vertically, ordered by vertical position
  (an arriving window that lands on another goes below it). Arrivals
  (keyboard, drop) and departures (keyboard, dragged out, closed) re-form
  the column, animated. The user said fixed slots would feel weird; it
  should be fluid. Still open: crowding (parking should hold 10-15,
  a stash 2-3); the user mentioned it may become tiling-like (windows also
  resized vertically to fit).

## Environment
- Fedora 44 KDE (aarch64) in a VMware Fusion VM on an Apple Silicon Mac,
  VMware SVGA3D virtual GPU. The user has VM snapshots to roll back to.
- User: scottjenson. Project at ~/Glance (was ~/WideMonitorUX). The user edits in VS Code,
  connected into the VM.
- KWin **6.7.5** (Plasma 6.7). Its source is unpacked at ~/src/kwin-6.7.5
  for checking internals (`dnf download --source kwin`, then
  `rpm2cpio kwin-*.src.rpm | cpio -idm` and untar).
- Build packages (installed): `kwin-devel`, `extra-cmake-modules`,
  `libepoxy-devel` (kwin-devel doesn't pull it in).
- Display: VMware "Use full resolution for Retina display" is on; the VM
  screen is about 6000x2450 px (it follows the VM window size). KDE scale
  changes: 200% earlier, **150%** since 2026-09-29 evening (4004x1630
  logical). Check with `kscreen-doctor -o` (desktop env, see below).

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
- `wayfire/`: the Wayfire 0.10.1 prototype (reference; see below).

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

## How the effect works (kwin/main.cpp)
- It is a KWin **effect**, not a plain `KWin::Plugin`, so `prePaintWindow`
  can call `data.setTransformed()` for scaled windows. Without that, KWin
  clips drawing in unscaled coordinates (`clipQuads` in
  scene/itemrenderer_opengl.cpp uses only the translation), so only the
  top-left part of a shrunk window is painted, and occlusion treats it as
  still covering its full-size area. `isActive()` is true only while
  something is scaled.
- Drawing: a `QTransform` on the window's `WindowItem` (item coordinates
  start at the frame's top-left corner).
- Input: an `InputEventFilter` at `InputFilterOrder::ButtonRebind` (very
  early). `Effect` has its own `pointerMotion` etc. virtuals, so the filter
  is a separate member object (`Filter`) that calls back.
- Quick tiling (`options->electricBorderTiling`) is turned off while loaded
  and restored on unload.

**Dragging** (`dragStep`, on `Window::interactiveMoveResizeStepped`): KWin
moves the real frame (grab offset kept as a fraction of the size); we draw it
scaled around the cursor. `edgeScale` solves the design rule in closed form:
the drawn edge is at d = cursorToScreenEdge − cursorToWindowEdge·s, and
s = minScale + (1 − minScale)·d/zoneWidth. Distances are converted to
original-size units (`grow` = original width / current width). If even
minScale doesn't fit, `shiftOntoScreen` slides it back on screen.
KWin's own move logic (window.cpp `nextInteractiveMoveGeometry`) also snaps
to edges (`adjustWindowPosition`) and keeps ≥100 px visible.

**Parked windows** (`dragFinished`, on `interactiveMoveResizeFinished`): state per
window in `m_parked` (`shown` rect in global coordinates, `original` size,
`restoring`). `applyParked` (also on every `frameGeometryChanged`) fits the
*current* frame into `shown` by width (scale = shown.width / frame.width),
so nothing jumps while the app catches up with a resize. Real resize via
`layoutSize`: exactly 1x or 2x the shown size if that is ≥ `minLayoutWidth`
(400), ≥ the app's minimum (`clientSizeToFrameSize(minSize())`) and
≤ original; else the smallest size keeping the shape that meets both
minimums, capped at the original (Firefox: 500 px wide). 1x/2x are for
text quality (drawn at 1/2, bilinear averages exact 2x2 blocks). Logs one
`glance:` line per resize. Dropped in main: `moveResize` to the
original size (grabbed spot under the cursor), `restoring` until it has it.

**Input to parked windows** (`route`, `reanchor`, `pick`): KWin picks the
window under the pointer from real (full-size) frames, *before* filters run
(`PointerInputRedirection::processMotionInternal` calls `update()` first).
So whenever the pointer is over a parked window, its frame is moved so the
point under the pointer is the same window point as in the drawing
(topLeft = pos − (pos − shown.topLeft)/scale), the drawing compensates, and
`pointer->update()` re-picks. KWin's own translation-only input mapping is
then exact at the pointer: clicks, hover, scrolling, title bar (drag out),
popups at the cursor. If KWin still picks another window (another frame
lies above), we point the seat at the right surface with our own
transformation (`transformFor`) and forward events ourselves; then
decorations don't respond. KWin doesn't notice that we re-pointed the seat
(it only calls `seat->notifyPointerEnter` when *its* focus window changes),
so before leaving events to KWin again, `syncSeatFocus` points the seat back
at KWin's focus window. Without it, after the pointer passed over a parked
window's invisible frame area, clicks on the parked window went to the
desktop with the window's coordinates (desktop rubber band). Not done during KWin's own moves
(`workspace()->moveResizeWindow()`). Resetting: KWin only recomputes the
pointer transformation on enter or geometry change.

**Tiny parked windows act like icons** (`holdPress`, `pendingMotion`,
`releasePending`; decided with the user 2026-09-29): parked windows drawn
below `iconBelow` (0.25) of their original size hold back a plain left
press. Moving more than `dragThreshold` (6 px) starts KWin's own move
(`performMousePressCommand(Options::MouseMove, pressPos)`; the frame was
re-anchored at the press, so the grabbed spot stays under the cursor);
releasing sooner delivers press + release to the app as a click (original
timestamp, window activated). Rationale: at that size the title bar is
too small to grab, and the idea is that apps reformat into widgets (e.g. a
music player becomes play/pause), so clicks and scrolling must still work.
Not for KDE title bars (`pointer->decoration()`), other buttons, or presses
with modifiers. Larger parked windows (the user expects people to use
windows at 50-60%) stay normal windows: nothing is taken from their content.

Unloading resizes parked windows back to their original size.

## Status (2026-09-29)
Running in the user's real Plasma session (via use-in-session.sh). Konsole
and Firefox (Wayland, client-side title bar with tabs) shrink, park, take
input, and drag back out; tiny parked windows drag from anywhere and pass
clicks through. Unlike the Wayfire version, a window dragged out of the
edge stays sharp-ish while growing (KWin samples the app's real buffer). Earlier, tested in the nested KWin, all working: shrink while dragging
(edge-driven rule), parking, clicks/selection/scrolling in parked windows,
dragging out by the title bar, real resize with reflow (Firefox, Konsole),
restore to original size. Open observations: some jank (possibly the
re-anchoring on every pointer motion, which moves the frame and repaints;
or VM load); one unexplained freeze in main that didn't recur.

## Next steps
1. Mouse accelerators: Meta+drag gestures (built 2026-09-30; see the
   header comment in main.cpp). While Meta is held during a drag, the
   window snaps (glides) to the gesture target as a preview; releasing the
   mouse commits, releasing Meta returns to a normal drag. Sideways walks
   the ladder parking L, stash L, left half, right half, stash R,
   parking R, one step per threshold (150 px, then every 250 px; a free
   window's first step is the half on that side). "In a half" (keyboard
   and gestures) = the window's horizontal extent and the half's share
   >= 80% (IoU, `halfMatch`), so a nudged window counts as in it. Up/down
   = Meta+Up/Down. A short diagonal (>= 100 px, under 150 px sideways) =
   the half on that side, full height if upward. The user felt snapping
   may be a bit quick; the distances are "pinned" for later tuning.
   Gesture distance is measured from our own recorded press position:
   KWin's `interactiveMoveResizeAnchor()` follows the cursor during a move.
2. Polish: re-anchor less often (e.g. on press/scroll, or after enough
   movement); overlapping parked windows; title-bar buttons of parked
   windows; text quality (the Wayfire halving scaler, below; pixel-exact
   placement at 1x/2x); fractional KDE scales.
3. Packaging (not yet: the user wants quality-of-life features first;
   shipping is the long-term goal): Fedora COPR / Arch AUR, like other
   third-party KWin effects (Better Blur, KDE Rounded Corners). Internal
   API, so it must be rebuilt per Plasma release. `README.md` (with
   install steps) is done; an MIT `LICENSE` file is not (metadata.json
   already says MIT). The user hasn't decided whether to hide
   their email in commits (GitHub noreply).
4. Ideas (not agreed): live resizing during the drag; fade icon-sized
   windows or cap them at an icon size; top/bottom edges; config options;
   multiple monitors (the on-screen fit blocks dragging to another one).
5. Long term: ask KWin upstream for a way to set a window's input
   transform. Fallback platform if KWin ever fails: a GNOME Shell extension.

## Wayfire prototype (wayfire/, reference only; keeps its old edge-shrink names)
Same behaviour, earlier version (cursor-driven 400 px zone). Build:
`meson setup wayfire/build && meson compile -C wayfire/build`; run
`wayfire -c ~/Glance/wayfire/wayfire-test.ini` from Konsole in the VM
window. Needs `wayfire`, `wayfire-devel`, `glm-devel` (installed). It worked
nested only with KDE at 100% (it can't tell KDE it is 2x). Worth porting
from it: `smooth_scaler_t`, which halves the texture into offscreen buffers
(exact 2x2 averages, like a mipmap) until the last step is between 1/2 and
1, then draws with bilinear; plain bilinear below 1/2 skips pixels and makes
text look dirty. Detailed notes on its internals and Wayfire 0.10.1 facts:
CLAUDE.md as of commit 6f217f6.

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
  (2026-09-30). If Meta+click stops working, check there first.
- Mac→VM clipboard (VMware Tools, `vmtoolsd -n vmusr`) is unreliable and
  adds a trailing NUL byte; keep commands for the user short or put them
  in scripts.
- Linux paths are case-sensitive.
