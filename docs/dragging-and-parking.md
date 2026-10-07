# Dragging, parking and input to parked windows

## Rules
- A window is full size while it lies entirely within main. Once its left
  or right (drawn) edge enters an edge zone, it shrinks around the cursor
  (the grabbed spot stays under it), linearly with that edge's depth into
  the zone, reaching `minScale` (0.15) at the screen edge. Scales are
  relative to the window's original size.
- Dropped while shrunk it is parked: it stays where and as large as
  drawn, fully usable. In a stash it is only zoomed (the app keeps its
  full size and never reflows; the shrinking gives depth). In parking it
  becomes a tile and its app is laid out as a phone-sized square. Dropped
  in main (scale ≥ `parkBelow`, 0.99) it gets its original size back.
- Windows in parking act like icons (below).

## Dragging
`WindowDrag::step` (kwin/drag.cpp), on `interactiveMoveResizeStepped`:
KWin moves the real frame (grab offset kept as a fraction of the size);
Glance draws it scaled around the cursor. `edgeScale` solves the rule in
closed form: the drawn edge is at d = cursorToScreenEdge −
cursorToWindowEdge·s, and s = minScale + (1 − minScale)·d/zoneWidth, in
original-size units (`grow` = original width / current width). If even
minScale doesn't fit, `shiftOntoScreen` slides it back on screen. KWin's
own move logic also snaps to edges and keeps ≥100 px visible.

A window dragged out of a stash starts at its stashed size (so a
Meta+wheel size isn't lost): `holdScale` remaps the edge rule
(`heldScale`) so its value at the drag's start gives that size, full size
stays full size and parking size parking size, linear in between.

## Parked windows
`WindowDrag::finished` → `ParkedWindows::park` (kwin/parked.cpp). Per
window in `m_parked`: `shown` (drawn rect, global), `original` size,
`restoring`. `applyParked` (also on every `frameGeometryChanged`) fits the
current frame into `shown` by width, so nothing jumps while the app
catches up with a resize.
- Parking is decided by the drawn size (`atParkingSize`: below
  `parkingScale`, which keeps icons at least `parkingMinSize`, 180 px, on
  the longer side). `layoutSize` lays a parking app out as a
  `parkingLayoutWidth` (600) square, each side at least the app's minimum
  (`clientSizeToFrameSize(minSize())`, Firefox 500) and no larger than the
  original's longer side, so web pages switch to their phone layout. The
  reflow happens as the window joins the parking column, which moves it
  anyway. Each resize logs one `glance:` line.
- Windows drawn smaller than their app (all stashed windows and parking
  icons) have no resize edges: KWin's borders sit around the invisible
  full-size frame. Meta+wheel resizes them. A parked window drawn exactly
  1:1 (rare) is picked with KWin's `hitTest`, which includes the resize
  borders; while KWin resizes it (`resizeStarted`/`resizeFinished`),
  `shown` follows the frame and it stays parked.
- Dropped in main: `moveResize` to the original size (grabbed spot under
  the cursor), `restoring` until it has it. Unloading resizes parked
  windows back to their original size.

### Logging out
Apps save their window size when closed at logout, so a parked Firefox
would reopen at its parking size. When a logout starts,
`ParkedWindows::fullSizeForLogout` gives every parked app its original
size before any app is asked to close; the windows stay parked and drawn
where they are. The signal: ksmserver moves KWin's session state out of
Normal first (`EffectsHandler::sessionStateChanged`). ksmserver's state
numbers are off by one from KWin's, so any state but Normal counts; a
manual "save session" doesn't change it. After a cancelled logout the
windows stay parked at full layout size (smaller text until moved).
Headless check: `setState` on KWin's /Session over D-Bus.

## Parking columns, free stashes
- Each parking area holds its windows as one column, centred vertically,
  ordered by vertical position (an arriving window that lands on another
  goes below it). Arrivals (keyboard, drop) and departures (keyboard,
  dragged out, closed) re-form the column, animated (`arrangeArea`;
  `arrange` and `leaving` act only on parking areas, `isParkingArea`).
  Parking is a managed dock, so tidying itself is expected there.
- Stashes have no column: a mouse drop stays exactly where and as large
  as drawn (windows may overlap; click to bring one forward), and leaving
  a stash moves nothing else. One rule for windows and clips alike.
  How far toward the edge you drop sets the size; a wider monitor has a
  bigger stash. Meta+arrows and Meta+drag snapping put a window in the
  middle of the stash zone, keeping its height. Declutter is the explicit
  tidy: it lines up the stashes it fills.

## Parking tiles
Every parking icon is a tile: drawn `parkingTile` (140 px) on its longer
side whatever size it was dropped at (about 10 per 1630 px edge), against
the screen edge (`parkingTileRect`, from `park`; a drop glides into it).
Clips keep their own shape. More icons than fit run past the column's
ends (not designed yet).

Tiles are turned away from the viewer (`tiltAngle`, 40°) around a
vertical axis at the screen edge (the outer edge stays in front). Each
column is one plane, seen in perspective from one eye level with the
middle of the usable area, `tiltDistance` (1) usable screen heights away,
so the gaps between tiles stay nearly parallel. A hover preview turns
flat as it grows; a window turns as it glides into parking and turns flat
in the first moments of a drag out. `ParkedWindows::updateTilt` (on every
draw transform change) picks the target (turned when in parking, not
previewed, not restoring); `advance` animates it.

How it's drawn: the draw transform stays a plain scale and move (the
focus ring width, mipmaps, Alt+Tab and input rely on that). The tilt
(`tiltTransform`, a perspective `QTransform`, `ParkedWindows::tiltOf`) is
applied only in the mipmap drawing, so tilted windows are always drawn
from mipmaps (KWin's paint data has no perspective). The focus ring is
put into a tilted window's image so it turns with it. A turned tile
reaches past its flat rectangle (up to ~40 px), which KWin wouldn't
repaint, so `repaintTilted` marks both on every transform change, tilt
step and content change. Input: `pick` hit-tests the turned shape
(`drawnContains`); re-anchoring and forwarding turn the pointer back flat
first (`untilt`, `transformFor`).

## Drawing small windows (mipmaps)
KWin draws a scaled window with bilinear sampling, which skips pixels
below half size: text breaks up and shimmers as it moves. `Mipmaps`
(kwin/mipmaps.cpp, from the effect's `drawWindow`) draws a window whose
draw transform is below `mipmapBelow` (0.5), or which is tilted, from a
full-size image of itself (`ItemRenderer::renderItem` into a texture, with
paint data that is the inverse of the item's transform) plus its mipmaps
(`GL_LINEAR_MIPMAP_LINEAR`).
- The image is redrawn only when the window's content changes
  (`EffectWindow::windowDamaged`: surfaces and title bar) or its
  full-size bounds do; drags and glides only redraw the quad.
- Full-size bounds = the window item's own `boundingRect()` at the
  frame's top-left, not `EffectWindow::expandedGeometry()` (that includes
  the transform).
- The focus ring is left out of the image and drawn over it, so it stays
  sharp (except on tilted windows, above).
- Cost: GPU memory for a full-size image per small window (~25 MB for an
  1800x1146 window at 150%), freed when it is drawn large again or
  closed. KWin's blur behind such windows is skipped.

## Input to parked windows
`ParkedInput` (kwin/parkedinput.cpp: `route`, `reanchor`) with
`ParkedWindows::pick`. KWin picks the window under the pointer from real
(full-size) frames, before filters run. So when the pointer is over a
parked window, its frame is moved ("re-anchored") so the point under the
pointer is the same window point as in the drawing (topLeft = pos − (pos
− shown.topLeft)/scale), the drawing compensates, and `pointer->update()`
re-picks. KWin's translation-only input mapping is then exact at the
pointer: clicks, hover, scrolling, title bar, popups at the cursor.
- If KWin still picks another window (another frame lies above), Glance
  points the seat at the right surface with its own transformation
  (`transformFor`) and forwards events itself (decorations then don't
  respond). KWin doesn't notice, so `syncSeatFocus` points the seat back
  at KWin's focus window before leaving events to KWin again.
- A frame move is a real geometry change in KWin (window rules, the
  window's monitor, the app is told), so motion and scrolling re-anchor
  at most once per refresh, forwarding in between; `anchorLate` lines the
  frame up a refresh after the pointer stops (for tooltips and menus).
  Presses and releases re-anchor at once. A move that would put the
  frame's centre on another monitor is forwarded instead (the frame
  swings far past the drawing, and KWin would give the window to that
  monitor).
- Not during KWin's own moves or any drag and drop.
- If this is ever too heavy: move the frame only on enter, pause, press
  and over the title bar.

## Icons
`ParkedInput::holdPress`/`pendingMotion`/`releasePending`, with
`ParkedWindows::isIcon`: windows in parking (any scale) and stashed
windows drawn below `iconBelow` (0.25) hold back a plain left press.
Moving more than `dragThreshold` (6 px) starts KWin's own move
(`performMousePressCommand(Options::MouseMove, pressPos)`); releasing
sooner delivers press + release to the app as a click (original
timestamp, window activated). Why: the title bar is too small to grab,
and clicks and scrolling must still reach the app (the idea: apps
reformat into widgets, a music player becomes play/pause). Not for KDE
title bars, other buttons, presses with modifiers, or clips (an app can
only start a drag from a press it received). Larger stashed windows stay
normal windows.

## Minimize = park
`ParkedWindows::minimizeToParking`: a window being minimized (title-bar
button, taskbar, shortcut, the app itself) is un-minimized at once
(`minimizedChanged`) and moved to the parking area on the side its drawn
centre is nearer to; one already in parking stays. KWin has already moved
focus on and minimized its dialogs, which come back with it. KDE's Squash
animation is redirected backwards before it starts, so nothing flashes.
Windows minimized at load, or appearing minimized, are parked once set
up. Windows Meta+arrows can't move (full screen, fixed size, not normal)
and windows a rule keeps minimized minimize as usual. Why: nothing should
be hidden where Alt+Tab and the map can't show it.
