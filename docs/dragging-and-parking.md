# Dragging, parking and input to parked windows (built)

## Design rules (agreed with the user)
- A window is full size as long as it lies entirely within main.
  Once its left or right (drawn) edge enters an edge zone, it shrinks,
  scaled around the cursor (the grabbed spot stays under it), linearly with
  that edge's depth into the zone, reaching `minScale` (0.15) exactly when
  the edge meets the screen edge.
- Dropped while shrunk: it stays exactly where and as large as drawn
  ("parked") and stays fully usable. In a stash it is only drawn
  smaller (zoom: the app keeps its full size); in parking the app is
  really resized to a phone-like width so web pages reflow (2026-10-05,
  see below). Dropped in main (scale ≥ 0.99): back to its original size.
- Scales are always relative to the window's original size.
- Tiny parked windows act like icons (decided with the user 2026-09-29),
  see below.

## Dragging
`WindowDrag::step` (kwin/drag.cpp), on `Window::interactiveMoveResizeStepped`: KWin
moves the real frame (grab offset kept as a fraction of the size); we draw it
scaled around the cursor. `edgeScale` solves the design rule in closed form:
the drawn edge is at d = cursorToScreenEdge − cursorToWindowEdge·s, and
s = minScale + (1 − minScale)·d/zoneWidth. Distances are converted to
original-size units (`grow` = original width / current width). If even
minScale doesn't fit, `shiftOntoScreen` slides it back on screen.
A window dragged out of a stash starts at the size it had there
(2026-10-03, user: a Meta+wheel size snapped back on the next drag; the
grab spot also changed the edge rule's size): `holdScale` remaps the edge
rule (`heldScale`, 2026-10-05) so its value at the drag's start gives that
size, full size gives full size and parking size parking size, linear in
between. Until 2026-10-05 it held the size until the edge rule reached it
or the window's center left the stash (then glided): moved outward after
being grabbed farther from its outer edge than at the drop, a window
stayed large across the stash and then snapped small (user: dragged in
small steps it didn't get smaller).
KWin's own move logic (window.cpp `nextInteractiveMoveGeometry`) also snaps
to edges (`adjustWindowPosition`) and keeps ≥100 px visible.

## Parked windows
`WindowDrag::finished`, on `interactiveMoveResizeFinished`: state per
window in `ParkedWindows::m_parked` (kwin/parked.cpp) (`shown` rect in global coordinates, `original` size,
`restoring`). `applyParked` (also on every `frameGeometryChanged`) fits the
*current* frame into `shown` by width (scale = shown.width / frame.width),
so nothing jumps while the app catches up with a resize. Real resize via
`layoutSize`, **zoom in the stash, reflow in parking** (2026-10-05,
talked through with the user): a stashed window keeps its app's full
size and is only drawn smaller, so it looks as it did while dragged
(the shrinking gives depth) and never reflows; a parking icon's app is
laid out as a `parkingLayoutWidth` (600) square (since 2026-10-06; before,
600 wide in its own shape), each side at least the app's minimum
(`clientSizeToFrameSize(minSize())`, Firefox 500), no larger than the
original's longer side: web pages switch to their phone layout and the
icon shows the app's compact form (hover previews are for reading). See
Parking tiles below for how icons are drawn.
The reflow happens as the window joins the parking column, which moves
it anyway. Parking is decided by the drawn size (`atParkingSize`, as
`parkedPlace`). Why: with 1x the shown size (≥ 400 px) stashed windows
reflowed but didn't look smaller (clips: same text size); the first try
that day (no reflow down to half size, then 2x the shown size) reflowed
pages at every drop below half size, which the user found disruptive.
Text quality: KWin's bilinear sampling reads 4 source pixels per screen
pixel, so below 1/2 thin strokes break up and shimmer: such windows are
drawn from mipmaps (see Drawing small windows below). Later maybe a
lower render scale for the app (`Window::setNextTargetScale`,
fractional-scale-v1) for sharp text at the stash's sizes. Trade-off: a stashed window is drawn below
1:1 and so has no resize edges (see Resizing parked windows below);
Meta+wheel resizes it. Logs one
`glance:` line per resize. Dropped in main: `moveResize` to the
original size (grabbed spot under the cursor), `restoring` until it has it.

Unloading resizes parked windows back to their original size.

**Logging out** (2026-10-04): apps remember their window size when they
are closed at logout, and Firefox reopened at its 500 px parked size,
which then became its "original". So at the start of a logout every
parked app really gets its original size back
(`ParkedWindows::fullSizeForLogout`), before any app is asked to close.
The windows stay parked, drawn where they are (scaled down further), so
nothing changes on the screen (the first version unparked them: they
grew in place for a moment before closing, which the user found
visually odd). The
moment: Plasma's ksmserver (`performLogout`) moves KWin's session state
out of Normal first (`EffectsHandler::sessionStateChanged`; KWin's
`SessionManager` isn't exported to plugins); apps are closed only after
its session save and plasma-fallback-session-save
(`plasma-shutdown`: `closeWaylandWindows`). ksmserver's numbers for the
states are off by one from KWin's (its Saving arrives as Quitting, its
Normal after a cancelled logout as Saving), so any state but Normal
counts. A manual "save session" doesn't change the state. After a
cancelled logout the windows stay parked at their full layout size
(smaller text until they are moved). Headless check: `setState` on
KWin's /Session over D-Bus brings a parked Konsole from 540x374 back to
1800x1246.

## Parking tiles
2026-10-06, the user's design, so parking looks different from the
stash and main: every parking icon is a **tile**, the app laid out
square (above), drawn `parkingTile` (140 px) on its longer side whatever
size it was dropped at, so about 10 fit along the 1630 px edge, against
the screen edge (`parkingTileRect`, from `ParkedWindows::park`; a drop
glides into it). Clips keep their own shape (their text sets their
height) but get the same size and tilt. More icons than fit: not
designed yet (they run past the column's ends).

Tiles are **turned away from the viewer** (`tiltAngle`, 40°) around a
vertical axis at their outer edge, in perspective (`tiltDistance`, the
eye 2.5 tile widths away): in left parking the left edge stays in front
at full height and the right edge recedes, mirrored on the right. A hover
preview turns flat as it grows (one animation); dropped or sent into
parking a window turns as it glides in; dragged out, it turns flat
during the drag's first moments. `ParkedWindows::updateTilt` (on every
draw transform change) picks the target: turned when in parking, not
previewed and not restoring; `advance` animates it (`animationTime`).

How it's drawn: the draw transform (a `QTransform` on the window item)
stays a plain scale and move, which Glance's other parts (focus ring
width, mipmaps, Alt+Tab, input) and KWin's repainting rely on; the
turned tile always lies inside that flat rectangle. The tilt
(`tiltTransform`, a perspective `QTransform`, `ParkedWindows::tiltOf`)
is applied only where the window is drawn from mipmaps: tilted windows
always are, at any size (KWin's paint data has no perspective, so its own
drawing can't turn them, and the mipmap image couldn't be drawn through
a perspective item transform). The GPU maps the texture in perspective.
The focus ring is put into a tilted window's image, so it turns with it.

Input goes through the tilt: `pick` hit-tests the turned shape
(`drawnContains`), and re-anchoring and forwarding turn the pointer
back flat first (`untilt`, `transformFor`), so clicks land on the right
spot of a tilted icon's app. Headless check: three minimized Konsoles
become 600x600 apps drawn as turned tiles in each parking column.

## Drawing small windows
`Mipmaps` (kwin/mipmaps.cpp), 2026-10-05, from the effect's `drawWindow`:
KWin draws a scaled window by blending the 4 window pixels nearest each
screen pixel (bilinear), so drawn below half size most pixels are
skipped: text and thin lines break up, and shimmer as the window moves.
A window whose draw transform is below `mipmapBelow` (0.5) is instead
drawn from an image of itself at full size (`ItemRenderer::renderItem`
into a texture, with paint data that is the inverse of the item's
transform: KWin applies the paint data's matrix just before the item's
transform) plus that texture's mipmaps (`GLTexture::allocate` with all
levels, `generateMipmaps`, `GL_LINEAR_MIPMAP_LINEAR`); the GPU blends
the two levels nearest the drawn size. The image is redrawn only when
the window's content changes (`EffectWindow::windowDamaged`: surfaces
and title bar) or its full-size bounds do; drags and glides only redraw
the quad. The full-size bounds are the window item's own
`boundingRect()` at the frame's top-left corner, not
`EffectWindow::expandedGeometry()`, which is where the window is drawn
(transform included: the first version sized the image from it and got
only the top-left part of the window). The focus ring is left out of
the image and drawn over it as usual (`renderItem` with a filter), so it
stays sharp. At 1/2 and larger KWin's own drawing is clean and mipmaps
would only soften it. Cost: GPU memory for a full-size image per small
window (an 1800x1146 window at 150%: about 25 MB with its mipmaps),
freed when it is drawn large again or closed. Blur behind such windows
(KWin's blur effect, later in the chain) is skipped. Headless check: a
parked Konsole at 45% loses its broken-up glyphs (the "@" in the prompt).

## Input to parked windows
`ParkedInput` (kwin/parkedinput.cpp): `route`, `reanchor`, and
`ParkedWindows::pick`: KWin picks the
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

Each frame move is a real geometry change in KWin (window rules, the
window's monitor, the app is told), so motion and scrolling move it at
most once per refresh (2026-10-04, code review finding 1); in between,
events are forwarded with `transformFor`, which is exact without moving
anything, and `anchorLate` lines the frame up a refresh after the pointer
stops (so tooltips and menus open in the right place). Presses and
releases move it at once. A move that would put the frame's centre on
another monitor is skipped (forwarded instead): the frame swings far past
the drawing, and KWin would give the window to that monitor. Possible
next step, if needed: move the frame only on enter, pause and press, and
over the title bar (the "hybrid", talked through with the user).

Resizing parked windows (2026-10-04): a parked window drawn exactly over
its frame (scale 1; since 2026-10-05 stashed windows are drawn smaller,
so this is rare)
is picked by KWin's own `hitTest`, which includes the decoration's
invisible resize borders outside the frame. Before, `pick` used only the
drawn rectangle, so a press in the resize border went to the window
behind and the resize never started. While KWin resizes a parked window
(`ParkedWindows::resizeStarted`/`resizeFinished`, from
`interactiveMoveResizeStarted`), `shown` follows the frame at the
window's scale; it stays parked at its new size (`original` unchanged).
Windows drawn smaller than their app (scale < 1: small windows that
since 2026-10-05 all stashed windows, and parking icons) have no
resize edges: KWin's borders sit around the invisible full-size frame.

## Tiny parked windows act like icons
`ParkedInput::holdPress`, `pendingMotion`,
`releasePending`, with `ParkedWindows::isIcon`; decided with the user 2026-09-29: windows in parking,
and stashed windows drawn below `iconBelow` (0.25) of their original size,
hold back a plain left press (`isIcon`). Parking counts whatever the
scale (fixed 2026-10-04): a narrow window is drawn above 0.25 there
(parking is at least `parkingMinSize`, 180 px, on the longer side), e.g. Firefox at
its 500 px minimum width (which it reopened at after a logout while
parked, fixed 2026-10-04, see Logging out); it got neither icon behaviour nor hover previews. Moving more than `dragThreshold` (6 px) starts KWin's own move
(`performMousePressCommand(Options::MouseMove, pressPos)`; the frame was
re-anchored at the press, so the grabbed spot stays under the cursor);
releasing sooner delivers press + release to the app as a click (original
timestamp, window activated). Rationale: at that size the title bar is
too small to grab, and the idea is that apps reformat into widgets (e.g. a
music player becomes play/pause), so clicks and scrolling must still work.
Not for KDE title bars (`pointer->decoration()`), other buttons, or presses
with modifiers. Larger parked windows (the user expects people to use
windows at 50-60%) stay normal windows: nothing is taken from their content.

## Minimize = park
Agreed 2026-10-02, built 2026-10-03 (`ParkedWindows::minimizeToParking`): parking is
Glance's minimize. A window being minimized (title-bar button, taskbar,
KDE's shortcut, the app itself) is un-minimized at once, from its
`minimizedChanged`, and moved (`moveTo`) to the parking area on the side
its drawn centre is nearer to; one already in parking stays. KWin has
already moved focus on (wanted: minimize means "out of the way") and
minimized its dialogs, which come back with it. KDE's Squash animation is
redirected backwards before it starts, so nothing flashes (confirmed by
the user). Windows already minimized at load, or appearing minimized, are
parked once set up. Windows Meta+arrows can't move (full screen, fixed
size, not normal) and windows a rule keeps minimized minimize as usual.
Why: nothing should be hidden where Alt+Tab and the map can't show it.
