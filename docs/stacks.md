# Stacks: stash and parking (built 2026-09-29; stash redesigned 2026-10-03)

## Parking: columns
Each parking area holds its windows as one column, centred vertically,
ordered by vertical position (an arriving window that lands on another
goes below it). Arrivals (keyboard, drop) and departures (keyboard,
dragged out, closed) re-form the column, animated (`arrangeArea`). The user
said fixed slots would feel weird; it should be fluid. Parking is a
managed dock, so tidying itself is expected there. Icons can be zoomed one
at a time with Meta+wheel; the column makes room ([meta-wheel.md](meta-wheel.md)).

## Stash: free placement
Stashes have no column (user's decision, 2026-10-03). Before, a window
dropped in a stash joined a column and moved after the drop, while text
dropped there (a clip) stayed put: the same gesture, two rules, hard to
predict, and the automatic placement felt heavy-handed. Now:
- A mouse drop stays exactly where and as large as it was drawn (the
  "respect mouse drags" rule). Windows may overlap; click to bring one
  forward. How far toward the edge you drop sets the size; a wider
  monitor (32:9) has a bigger stash.
- Leaving a stash moves nothing else.
- Keyboard (Meta+arrows) and Meta+drag snap still put a window in the
  middle of the stash zone, keeping its height (it may overlap).
- Declutter is the explicit tidy: it lines up the stashes it fills
  (`arrangeArea` called from `declutter`).
- Meta+wheel resizes a stashed window in place ([meta-wheel.md](meta-wheel.md)).

## How it works
`arrange` and `leaving` only act on parking areas (`isParkingArea`: areas
0 and 2 of `areaOf`). Still open: crowding of parking (10-15 icons), see
[plans/backlog.md](../plans/backlog.md).
