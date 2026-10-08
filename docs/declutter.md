# Declutter: Meta+double-click, and shake

## Design
On a window: it fills the half of main nearest to it at full height and
every other window in main goes to a stash. On a stash or parking window:
the same (it comes forward). On the empty desktop: main is cleared. The
same Meta+double-click again undoes the last declutter. Stashes are
balanced (by count, windows keep their left-to-right order), and each
stash gets one common scale that fits its column, so it lines up.

Shake: shaking a window while dragging it (title bar or Meta+drag) does
only the scatter: every other window in main goes to a stash, the same
way, and the drag goes on, so the window lands wherever it is dropped.
A shake is three changes of horizontal direction within half a second,
each stroke at least 30 px. No undo.

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

`toggle` is `snapshot` (for undo), `fillHalf` for the target, then
`scatter(except, output)`. The shake calls only `scatter` (via
`Declutter::shake`, which also drops the undo snapshot).
`WindowDrag::shakeStep` watches the pointer (not the window, so Meta+drag's
lead doesn't count) on every drag step: a change of direction is moving
back `shakeStroke` from the farthest point; `shakeTurns` within
`shakeTime` (kwin/tuning.h) shake. Log: `glance: <title>: shaken, scatter`.
