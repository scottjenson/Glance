# Meta+drag: acceleration and pause to snap

Part of the `WindowDrag` component (kwin/drag.h/.cpp). Meta+drag moves a
window from anywhere inside it, like a title-bar drag, plus:

## Acceleration (`leadStep`)
Horizontally the window gets ahead of the pointer (`m_leadX`; the pointer
itself can't be moved, VMware's pointer is absolute). Gain grows from 1 to
`leadMaxGain` (4) over a run of `leadBuild` (5% of screen width) in one
direction; a reversal (≥ `reversalJitter`, 5 px) or moving slower than
`leadSlow` (300 px/s) restarts at 1:1, so corrections are precise. Screen
edges stop the window and overshoot isn't stored. Without Meta: gain 1,
no snapping.

## Pause to snap (`snapTargetAt`)
Holding still `snapDwell` (500 ms) snaps to the region the window's center
is in: the parking band (`parkingBand`, outer 15% of an edge zone) →
parking; the rest of the zone → stash (centered); main → a half at full
height, or all of main at full height in the middle band (`snapFullBand`,
20% of the screen width). Position-based, so the pause shows what will
happen. After the first snap the drag stays in snapping mode: moving into
another region snaps there with a short glide, no sizes in between; the
region follows the pointer's movement since the snap (`m_snapAnchor` +
delta) so nothing jumps. Release keeps the target; releasing Meta leaves
snapping mode.

## Details
- The drag's start place uses Glance's own recorded press: KWin's
  `interactiveMoveResizeAnchor()` follows the cursor.
- A Meta+drag activates and raises the window: KDE's Meta+left-drag
  mouse command (`commandAll1`, "Move") is set to "Activate, Raise and
  Move" while Glance is loaded (`KdeIntegration::activatingMetaDrag`,
  kept on settings reloads, restored on unload). Activating from the
  drag's first step instead didn't work (the ring stayed on the old
  window).
- All numbers are first guesses, to tune by feel.
