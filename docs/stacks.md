# Stacks: stash and parking columns (built 2026-09-29)

Each stash and parking area holds its windows as one column, centred
vertically, ordered by vertical position (an arriving window that lands on
another goes below it). Arrivals (keyboard, drop) and departures (keyboard,
dragged out, closed) re-form the column, animated (`arrangeArea`). The user
said fixed slots would feel weird; it should be fluid.

Known tension with "respect mouse drags": re-forming the column moves a
window that was dropped into it. Still open (see
[plans/backlog.md](../plans/backlog.md)): drops that keep their exact spot,
and crowding (parking should hold 10-15, a stash 2-3); the user mentioned it
may become tiling-like (windows also resized vertically to fit).
