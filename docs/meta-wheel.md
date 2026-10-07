# Meta+wheel: resize in place

Dragging decides where a window goes; Meta+wheel decides how big it is.
Scrolling up grows the window under the pointer, down shrinks it,
anchored at the pointer (the point under it stays put); nothing else
moves.
- **Main**: a real resize, both sides by the same factor, from 300x200
  (`wheelMinWidth`/`Height`, or the app's minimum) up to the usable
  screen area, kept on screen.
- **Stash**: drawn larger or smaller at once, between just above parking
  size and full size; the app gets the new size once the scrolling stops
  (`wheelSettle`, 400 ms). It stays a stash window, and the next drag
  starts at that size (`holdScale`, [dragging-and-parking.md](dragging-and-parking.md)).
- **Parking icon**: sizes its hover preview: up opens it at once and grows
  it, down shrinks it; from icon size up to the width of the edge zone
  (covers the stash, never main), the screen height and the original
  size. It stays a preview, closing when the pointer leaves. Shrunk to
  icon size it closes, and `m_noPreview` keeps it closed until the
  pointer leaves. Grown past the app's size, the app is resized to it
  when the scrolling stops (sharp, not upscaled) and back when it closes.
  Clips: up to 1:1.

## How it works
The `MetaWheel` component (kwin/wheel.h/.cpp): `axis` (from `onAxis`):
Meta alone, vertical, no buttons, no move or drag; the target is `pick`
(the window drawn there). Factor `exp(-delta * step)`, step
`wheelStepWheel` (10% per 15-unit notch) or `wheelStepFinger` for
touchpads. `resizeInMain`, `resizeInStash` (+ `settle`), `resizePreview`
(+ `settle` grows the app; `HoverPreviews::shrinkApp` in `close` shrinks
it back, and its `closing` signal calls off a pending `settle`).
