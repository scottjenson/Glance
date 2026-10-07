# Declutter: Meta+double-click

## Design
On a window: it fills the half of main nearest to it at full height and
every other window in main goes to a stash. On a stash or parking window:
the same (it comes forward). On the empty desktop: main is cleared. The
same Meta+double-click again undoes the last declutter. Stashes are
balanced (by count, windows keep their left-to-right order), and each
stash gets one common scale that fits its column, so it lines up.

## How it works
The `Declutter` component (kwin/declutter.h/.cpp: `button`, `toggle`,
`undo`), with `ParkedWindows::fillHalf` and `fittingScale`. The first
Meta+click goes to KWin (its Meta+press move ends without motion); a
second Meta+left press within Qt's double-click interval and
`dragThreshold` is taken, with its release. Panels etc. are ignored
(`manageable`). Undo snapshot `m_last`: every manageable window on that
output (parked state, or frame + maximize mode); it counts as "again" only
if the target is still in the half it was put in (desktop:
`Declutter::desktop`). If no window leaves main, stashes aren't rescaled.
