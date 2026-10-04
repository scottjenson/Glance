# Refactor: splitting kwin/main.cpp into components

Agreed with the user 2026-10-04 (code review finding 5,
[code-review.md](code-review.md)): option B, staged. Real components with
their own state and a small interface, not just the one class spread over
several files. No behaviour change in any stage; each stage is its own
commit, checked with the unit tests (`ctest --test-dir kwin/build`) and
then by the user (log out and in, the smoke test below).

## Stages
1. **Geometry and unit tests.** Tuning constants and the Place enum go to
   `kwin/tuning.h`; the pure math (edge scale, places and their rects,
   layout size, parking scale, fitting scale, spreading piles, small rect
   helpers) to `kwin/geometry.h/.cpp`, taking plain rects and sizes
   instead of `Window*`. Qt Test tests in `kwin/tests/`, no KWin needed.
2. **One per-window state record.** `m_parked`, `m_wasFull`, the recent
   list and the rest merged into one struct per window, erased in one
   place when a window closes.
3. **Self-contained features become components**, one commit each:
   clips (finding 4's fixes land here), Alt+Tab (hunt, map, label),
   declutter, hover previews, Meta+wheel.
4. **The coupled core:** dragging and Meta+drag, parking and animation,
   input routing. `Glance` becomes a coordinator that owns the components
   and decides, in one visible place, which one gets an input event first.

Target: a dozen files of 150-500 lines, plus a small `glance.cpp`.

## Status
- Stage 1 done 2026-10-04: tuning.h, geometry.h/.cpp (glance-core
  library), 30 unit tests; main.cpp 3,857 -> 3,534 lines. Checked by the
  user.

## Smoke test (the user, after each stage, ~5 minutes)
1. Drag a window to each edge: it shrinks; drop it in a stash and in
   parking. Drag it back to main: full size again.
2. Meta+Left/Right through all places; Meta+Up, Meta+Down.
3. Use a stashed window: hover, tooltip, text selection, scroll,
   right-click menu, title-bar buttons.
4. Parking icon: hover preview, click goes to the app, drag moves it;
   Meta+wheel over it sizes the preview.
5. Minimize a window: it parks.
6. Meta+drag with a pause (snaps); Meta+wheel on a stashed window.
7. Meta+double-click (declutter), again (undo).
8. Alt+Tab: tap, and hold for the map.
9. Drop text on the desktop, and Meta+C: clips appear; drag a clip into
   a text editor.
10. Focus ring follows Meta+Alt+arrows, with the bounce.
