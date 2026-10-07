# Hover previews

## Design
Hovering a parking icon turns it flat and grows it in place to
`previewGrow` (2.5x, about 350 px: a 600 px phone layout at 58%), in one
animation, anchored at the screen edge and centered on its spot, over its
neighbours, which stay put and partly visible. Grown icons keep icon
behaviour (drag anywhere, clicks pass through). Scrubbing: the first
preview opens after `previewDelay` (300 ms), then moving along the column
switches at once (by home spots, both animating). Leaving: it shrinks and
turns away again after `previewGrace` (300 ms). Meta+wheel over an icon
sizes its preview ([meta-wheel.md](meta-wheel.md)).

## How it works
The `HoverPreviews` component (kwin/previews.h/.cpp: `update`, `iconAt`,
`previewRect`, `open`, `close`).
- `Parked::preview` is drawn instead of `shown` (`displayRect`), so
  drawing, picking and re-anchoring follow it; `shown` (the home spot) is
  untouched.
- `iconAt` hit-tests home spots (± half `arrangeGap`), also under the
  grown window, and wins over it, so scrubbing works through it.
- Preview = `previewGrow` x the home spot, capped at 1:1 with the current
  frame and the screen height. Grown windows are raised.
- `current` notices a preview reset elsewhere (a clip drag resets it
  directly).
- Not while a button is held, a move is on, or a drag and drop. Only
  windows in parking (`isPreviewable`), not icon-sized stashed windows.
