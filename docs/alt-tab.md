# Alt+Tab: hunt and return, the desktop map (built, phase 1)

Designed and built 2026-10-02; tested by the user the same day.

## Why Glance needs it
Glance's side windows are visible and usable in place (type, sort, play or
pause music without bringing them to main), so the classic job of Alt+Tab,
showing windows buried behind each other, mostly falls away. What Glance
lacked is:
- **Time:** everything is spatial (click, Meta+Alt+arrows); nothing said
  "back to the window I was just in". Reaching one parking icon among many
  with Meta+Alt+arrows is tedious.
- **Identity:** at icon size you can see that a window is there, but not
  what it is.
- **Head-turning:** on an ultrawide, the sides are peripheral; hunting for a
  highlighted window out there means turning your head.

## The model: hunt and return (user's, from the Mac)
You Alt+Tab three or four times to find something buried, then a quick
Alt+Tab takes you back; from then on the two windows toggle with a tap.
Recency order (most recently used first) gives this for free: the chosen
window becomes first, the one you came from second.

- Unit: **windows**, not apps (the toggle needs a specific window to return
  to). A Mac-style app switcher was considered and set aside.
- **Alt+Tab never moves windows.** It only focuses; the chosen window gets
  the focus ring and bounce where it is. Moving is Meta's job.
- Windows: all visible windows (one desktop). Glance has no minimized
  windows by design (parking is its minimize).

## Interaction
- **Tap** (Alt+Tab, released quickly): switch to the previous window.
  Nothing else visible happens: ring + bounce.
- **Hold** (Alt still down after `holdDelay`, 200 ms): the **map** appears.
  - The whole desktop is drawn at `mapScale` (50%) in the centre of the
    screen, dimmed (`mapDim`). 50% of the width is exactly main's width:
    the whole desktop folds into the region you're looking at. It's a map,
    not a list: the layout stays the same (unlike KDE's Overview or
    Mission Control), so hunting still teaches where things live.
  - Overlapping windows spread (below). Shrinking and spreading happen in
    one movement (`mapTime`, 200 ms): each window goes straight to its
    place. (First built as two 150 ms stages; the user found "boom, boom"
    less solid, 2026-10-02.)
  - Each Tab moves the selection to the next window in recency order;
    Shift+Tab goes back. The selected window is drawn undimmed and gets
    the **focus ring and bounce** (user, 2026-10-02: undimming alone
    doesn't show a dark window on a dark background). Tab presses during
    the animation still count.
  - **Label** on the selected window: the app's icon (40 px) and the
    window title (22 px) on one line, on a rounded translucent card,
    centred on the window with its bottom on the window's bottom edge
    (wider than the window if need be, covering a little of it). It is
    always in the same spot, inside the window, so it can always be drawn.
    History (2026-10-02): first in the centre of the screen, large (icon
    128 px above a 36 px title); distracting, the eye had to jump. Then
    under the window; in a column, Tabbing from the bottom window to the
    one above moved the label from below one to just under the next,
    confusing. The user chose bottom-aligned inside the window.
  - Release Alt: the map zooms back; the selected window gets focus in
    place (ring + bounce). Esc: cancel, back as before.
  - Panels, notifications and anything else not in the map fade out. The
    wallpaper shrinks with the map; the bands above and below are black.
  - The pointer does nothing while switching (clicks, wheel and motion
    are swallowed; the cursor still moves).
- **Drawing only**: real frames never move, so apps don't re-lay out and
  the return is exact.

## Spreading piles
Goal (user): every window **countable** and **pointable**, not fully
visible; the label identifies it.
- A **pile** = free (not parked) windows that overlap by more than
  `pileOverlap` (10%) of the smaller one. Stash windows' slight overlaps
  and parking columns don't count.
- The front window of a pile stays in place. The others go, alternately,
  into a row above and a row below the pile, within its width and the
  space up to the screen edge, shrunk until the row fits (like temporary
  vertical stashes, the user's idea). For full-height windows in main at a
  50% map: front + 2 above + 2 below fit at another half; more shrink.
- Pushing sideways was rejected: the map's own stashes and parking sit
  left and right, so it must stay truthful about left and right.

## Keys and KWin
- Alt+Tab and Meta+Tab (also with Shift). KWin's eight "Walk Through
  Windows" actions (incl. "of Current Application", Alt+\`) are disabled
  while loaded (`disableKdeShortcuts`); they belong to the TabBox object,
  reached via `workspace()->tabbox()` (its header isn't installed; it
  derives from QObject only, so the pointer is reinterpreted).
- While switching, no surface has keyboard focus
  (`setFocusedKeyboardSurface(nullptr)`, as KDE's own switcher does via
  `pickFocus`), so apps see neither the keys nor a lone Alt press/release
  (Firefox's menu bar). Focus comes back with `input()->keyboard()->update()`
  in a `singleShot(0)` after the releasing key event has gone through.
- Under Alt, Tab and Esc are swallowed (and marked filtered, so their
  releases aren't sent). Under Meta they are passed on, as for
  Meta+arrows (else KDE opens the Meta-tap launcher); the disabled
  actions make them do nothing.
- Shift+Tab arrives as `Qt::Key_Backtab`.
- Recency: Glance keeps its own list (`m_recent`, front on
  `windowActivated`; seeded from the stacking order at load). KWin's
  `FocusChain` isn't exported to plugins.

## How it works (the `AltTab` component, kwin/alttab.h/.cpp)
- `key` / `startSwitch` / `step` / `finishSwitch`; `m_hold` opens the
  map (`openMap`); `closeMap` animates back from wherever it is.
- `paintWindow`: on top of everything else (parked transforms, bounce),
  the paint data's scale and translation take each window from where it
  is drawn (`currentlyDrawn`) to `mapped(...)`: drawn point = item
  position + translation + scale · item-local point (see
  `createRenderNode` in scene/itemrenderer_opengl.cpp), so
  `t' = to - item + k·(item + t - from)`.
- **Clipping gotcha:** the map sets `PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS`,
  and KWin then gives windows an infinite region; the renderer then clips
  quads in software against the screen using only the translation
  (`clipQuads`), cutting shrunk windows off at the right and bottom (the
  first build looked like a 37% map). Passing a finite region
  (`viewport.deviceRect()`) makes it use GL scissor clipping instead.
- The focus ring goes to the `highlighted()` window (the selection while
  switching, then the chosen one until it's active); its width is divided
  by the map's scale too (`mapZoom`), updated every frame while the map
  is up. The ring itself stays in the effect: AltTab's `ringChanged` and
  `bounce` signals tell it when to follow or bounce.
- Label: `labelImage` paints icon + title with QPainter, uploaded as a
  `GLTexture`, drawn in `paintScreen` with the MapTexture|Modulate shader
  (opacity follows the map); redone when the selection or its title
  changes.
- Log lines: `glance: switch started`, `map shown (N windows, M spread)`,
  `window chosen: ...`, `switch cancelled`.
- Testing without a keyboard: `GLANCE_TEST_MAP=1` opens the map 3 s after
  loading (see CLAUDE.md for the headless screenshot loop).

## Phase 2 (agreed to wait until phase 1 had been used)
- Mouse in the map: click a window to choose it.
- App grouping hint: the selected window's sibling windows lightly
  undimmed.
- Tuning by feel: map scale (50%), dim level, hold delay, animation time,
  pile threshold.
