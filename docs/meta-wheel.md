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
- **Parking icon**: sizes its hover preview (2026-10-03, second
  version): up opens it at once (no wait) and grows it, down shrinks it;
  from icon size up to the width of the edge zone (covers the stash,
  never main), the screen height and the original size. It stays a
  preview: it closes when the pointer leaves, so nothing is left in the
  way. Shrunk to icon size it closes and stays closed until the pointer
  leaves. Grown past the app's size, the app is resized to it when the
  scrolling stops (sharp, not upscaled) and back when it closes. Clips:
  up to 1:1 (their layout is their own).
  First version (replaced): a lasting zoom between icon and preview
  size, the column making room like a dock. The user: since hovering
  already opens the preview, the wheel could only shrink it; growing for
  good past it would cover the stash.

## How it works
The `MetaWheel` component (`kwin/wheel.h/.cpp`): `axis` (from `onAxis`): Meta alone, vertical, no buttons, no move or
drag; target is `pick` (the window drawn there). Factor
`exp(-delta * step)`, step `wheelStepWheel` (10% per 15-unit notch) or
`wheelStepFinger` for touchpads. `resizeInMain`, `resizeInStash`
(+ `settle`), `resizePreview` (+ `settle` grows the app,
`HoverPreviews::shrinkApp` in `close` shrinks it back, and its `closing`
signal calls off a pending `settle`).

A stash size set this way survives the next drag: the drag starts at that
size and shrinks or grows from there (see `holdScale` in
[dragging-and-parking.md](dragging-and-parking.md)).

## To check by feel
Scroll speed and direction (natural scrolling in the VM from the Mac
trackpad may flip it), the 400 ms settle.
