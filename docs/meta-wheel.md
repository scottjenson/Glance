# Meta+wheel: resize in place (built 2026-10-03)

## Design (agreed with the user 2026-10-03)
Dragging decides where a window goes; Meta+wheel on a released window
decides how big it is. Scrolling up grows the window under the pointer,
down shrinks it, anchored at the pointer (the point under it stays put),
nothing else moves (except in parking, below).
- **Main**: a real resize, both sides by the same factor, from 300x200
  (or the app's minimum) up to the usable screen area, kept on screen.
- **Stash**: drawn larger or smaller at once, between just above parking
  size and full size; the app gets the new size (`park`) once the
  scrolling stops (`wheelSettle`, 400 ms). It stays a stash window.
- **Parking icon**: zooms between icon size and its hover-preview size,
  never more, so it stays in parking ("no breaking out", user's rule).
  One icon at a time; the column makes room like a slow, controlled Mac
  dock: the windows above and below stay packed against it and slide
  away (or back). A column that would go off screen shifts back on; one
  that can't fit stops the zoom. The zoom is a "right now" thing: it is
  lost when the window leaves parking. A preview open on the icon is the
  starting size; the icon doesn't preview again until the pointer leaves.

## How it works
`metaWheel` (from `onAxis`): Meta alone, vertical, no buttons, no move or
drag; target is `pick` (the window drawn there). Factor
`exp(-delta * step)`, step `wheelStepWheel` (10% per 15-unit notch) or
`wheelStepFinger` for touchpads. `resizeInMain`, `resizeInStash`
(+ `settleWheel`), `zoomIcon` (+ `moveInColumn`). `Parked::zoom` holds the
icon's zoom; `shown` is the zoomed rectangle, and everything that judges
a window's place by its scale uses `baseWidth` (shown width / zoom).

## To check by feel
Scroll speed and direction (natural scrolling in the VM from the Mac
trackpad may flip it), the 400 ms settle.
