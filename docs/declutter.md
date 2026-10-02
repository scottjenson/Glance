# Declutter, Meta+double-click (built 2026-10-01)

## Design
From the user's talk, where it was a shake gesture, now Meta+double-click:
on a window, it fills the half of main nearest to it at full height and
every other window in main goes to a stash; on a stash/parking window, same
(it comes forward); on the empty desktop, main is cleared. The same
Meta+double-click again undoes (only the last declutter). Stashes are
balanced (count-based, windows keep their left-to-right order) and each
stash gets one common scale that fits its column (so it lines up).
Follow-ups: [plans/backlog.md](../plans/backlog.md).

## How it works
`metaDoubleClick`, `declutter`, `fillHalf`,
`fittingScale`, `undoDeclutter`: the first Meta+click goes to KWin (its
Meta+press move ends without motion); a second Meta+left press within
Qt's double-click interval and `dragThreshold` is taken, with its release.
Panels etc. are ignored (`manageable`). Undo snapshot `m_declutter`: every
manageable window on that output (parked state or frame + maximize mode);
it counts as "again" only if the target is still in the half it was put
in (desktop: `Declutter::desktop`). If no window leaves main, stashes
aren't rescaled. `moveTo`/`placeRect`/`commitPlace` take a stash scale.
