# Meta+drag: acceleration and pause to snap (built 2026-10-01/02)

Designed with the user 2026-10-01 after two rounds: distance-based snapping
spoiled Meta+drag as "grab anywhere and move a bit"; velocity throws felt
chaotic, burned through the ladder, gave no feedback; see `leadStep`.

Meta+drag moves like a title-bar drag, but horizontally the window gets
ahead of the pointer (`m_leadX`; the pointer can't be moved: VMware's
pointer is absolute). Gain grows from 1 to `leadMaxGain` (4) over a run of
`leadBuild` (5% of screen width) in one direction; a reversal
(>= `reversalJitter`, 5 px, against it) or moving slower than `leadSlow`
(300 px/s) restarts at 1:1, so corrections are precise (user's Fitts's-law
point). Screen edges stop the window and overshoot isn't stored.

Holding still `snapDwell` (500 ms) snaps to the region the window's center
is in (`snapTargetAt`, 2026-10-02): parking band (`parkingBand`, outer 15%
of an edge zone) -> parking, rest of the zone -> stash (centered), main ->
left/right half at full height, or all of main at full height in the middle
band (`snapFullBand`, 20% of the screen width) (option A, chosen by the
user: "position-oriented", the pause shows what will happen). Before, a half
snap kept the original height and looked square. After the first snap the
drag stays in snapping mode (user: "calmer"): moving into another region
snaps there with a short glide, no sizes in between; the region follows the
pointer's movement since the snap (`m_snapAnchor` + delta) so nothing jumps.
Release keeps the target; releasing Meta leaves snapping mode (follow the
pointer again). Without Meta: gain 1, no snapping. Vertical throws/fill were
dropped (Meta+Up/Down remain).

All numbers are first guesses to tune by feel. User verdict (2026-10-01):
"feels like I'm in control"; the pointer not lining up with the window is
"a little weird" but worth the trade-off. Snapping mode (2026-10-02): "works
well enough"; tuning may follow. The drag's start place uses our own
recorded press: KWin's `interactiveMoveResizeAnchor()` follows the cursor.

A Meta+drag activates and raises the window (2026-10-03, user: the
dragged window didn't get the focus ring). KDE's Meta+left-drag is its
mouse command "Move" (`commandAll1`), which doesn't activate; while
Glance is loaded it is "Activate, Raise and Move" (`activatingMetaDrag`,
kept on settings reloads, restored on unload), as a title-bar drag does
with its press. Tried first and didn't work: activating from `dragStep`
at the start of the move (the ring stayed on the old window).
