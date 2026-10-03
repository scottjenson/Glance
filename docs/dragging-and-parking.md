# Dragging, parking and input to parked windows (built)

## Design rules (agreed with the user)
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
- Tiny parked windows act like icons (decided with the user 2026-09-29),
  see below.

## Dragging
`dragStep`, on `Window::interactiveMoveResizeStepped`: KWin
moves the real frame (grab offset kept as a fraction of the size); we draw it
scaled around the cursor. `edgeScale` solves the design rule in closed form:
the drawn edge is at d = cursorToScreenEdge − cursorToWindowEdge·s, and
s = minScale + (1 − minScale)·d/zoneWidth. Distances are converted to
original-size units (`grow` = original width / current width). If even
minScale doesn't fit, `shiftOntoScreen` slides it back on screen.
A window dragged out of a stash keeps its size at first (2026-10-03, user:
a Meta+wheel size snapped back on the next drag; the grab spot also changed
the edge rule's size): `holdScale` holds it until the edge rule reaches
that size (then follows it, no jump) or the window's center leaves the
stash (then glides to the edge rule).
KWin's own move logic (window.cpp `nextInteractiveMoveGeometry`) also snaps
to edges (`adjustWindowPosition`) and keeps ≥100 px visible.

## Parked windows
`dragFinished`, on `interactiveMoveResizeFinished`: state per
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

Unloading resizes parked windows back to their original size.

## Input to parked windows
`route`, `reanchor`, `pick`: KWin picks the
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

## Tiny parked windows act like icons
`holdPress`, `pendingMotion`,
`releasePending`; decided with the user 2026-09-29: parked windows drawn
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

## Minimize = park
Agreed 2026-10-02, built 2026-10-03 (`minimizeToParking`): parking is
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
