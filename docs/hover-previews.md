# Hover previews (built 2026-09-30; redesigned 2026-10-01)

## Design
Hovering a parking icon grows it **in place** to 2x
(`previewGrow`), anchored at its edge and centered on its spot, over its
neighbours, which stay put and about half visible (user's idea). The first
fly-out version moved the window beside the column, away from the pointer,
so it couldn't be grabbed, and big apps (1:1 layout) got huge. Grown icons
keep icon behaviour (drag anywhere, clicks pass through). Scrubbing: first
after 300 ms, then switching at once by home spots, both animating
simultaneously (user's explicit wish). Leaving: shrinks after 300 ms. 2x may
be too small to read; 2.5x is the fallback.

## How it works
`updateHover`, `iconAt`, `previewRect`,
`openPreview`, `closePreview`: `Parked::preview` is drawn instead of
`shown` (`displayRect`), so drawing, picking and re-anchoring follow it;
`shown` (the home spot) is untouched. `iconAt` hit-tests home spots
(± half `arrangeGap`), also under the grown window, and wins over it (so
scrubbing works through it). Preview = `previewGrow` x the home spot,
capped at 1:1 with the current frame and the screen height, at the
screen edge, centered on the spot. Grown windows are raised. Timers
`m_previewOpen` (`previewDelay`) and `m_previewClose` (`previewGrace`).
Not while a button is held, a move is on or a DnD drag. Only windows in
parking (`isPreviewable`): since stashes are free placement, a stashed
window dropped near the edge can be icon-sized too, and it previewed by
mistake (fixed 2026-10-03).

## Meta+wheel zoom
A Meta+wheel zoom (see [meta-wheel.md](meta-wheel.md)) is the lasting
version of a preview: capped at the preview size, it makes room in the
column instead of covering neighbours. `previewRect` goes by the icon's
unzoomed size (`baseWidth`), so a zoomed icon still previews to the same
size. After a zoom, `m_noPreview` keeps that icon from previewing until
the pointer leaves it.
