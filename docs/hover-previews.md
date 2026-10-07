# Hover previews (built 2026-09-30; redesigned 2026-10-01)

## Design
Hovering a parking icon turns it flat and grows it **in place** to 2.5x
(`previewGrow`; 2x until 2026-10-06, when icons became 140 px tiles,
[dragging-and-parking.md](dragging-and-parking.md), Parking tiles: 2.5x
is about 350 px, a 600 px phone layout at 58%; 3x, tried first, hid the
neighbours above and below completely), both in one animation,
anchored at its edge and centered on its spot, over its
neighbours, which stay put and about half visible (user's idea). The first
fly-out version moved the window beside the column, away from the pointer,
so it couldn't be grabbed, and big apps (1:1 layout) got huge. Grown icons
keep icon behaviour (drag anywhere, clicks pass through). Scrubbing: first
after 300 ms, then switching at once by home spots, both animating
simultaneously (user's explicit wish). Leaving: shrinks (and turns away
again) after 300 ms.

## How it works
The `HoverPreviews` component (`kwin/previews.h/.cpp`): `update`,
`iconAt`, `previewRect`, `open`, `close`: `Parked::preview` is drawn instead of
`shown` (`displayRect`), so drawing, picking and re-anchoring follow it;
`shown` (the home spot) is untouched. `iconAt` hit-tests home spots
(± half `arrangeGap`), also under the grown window, and wins over it (so
scrubbing works through it). Preview = `previewGrow` x the home spot,
capped at 1:1 with the current frame and the screen height, at the
screen edge, centered on the spot. Grown windows are raised. Timers
`m_open` (`previewDelay`) and `m_close` (`previewGrace`). `current`
notices a preview reset elsewhere (a clip drag resets it directly).
Not while a button is held, a move is on or a DnD drag. Only windows in
parking (`isPreviewable`): since stashes are free placement, a stashed
window dropped near the edge can be icon-sized too, and it previewed by
mistake (fixed 2026-10-03).

## Meta+wheel
Meta+wheel over an icon sizes its preview, up to the width of the edge
zone ([meta-wheel.md](meta-wheel.md), `resizePreview`). Shrunk back to
icon size it closes, and `m_noPreview` keeps it from previewing again
until the pointer leaves.
