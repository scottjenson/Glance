# Alt+Tab: hunt and return, the desktop map

## Why Glance needs it
Glance's side windows are visible and usable in place, so Alt+Tab's
classic job (showing windows buried behind each other) mostly falls away.
What it adds:
- **Time:** "back to the window I was just in"; reaching one parking icon
  among many with Meta+Alt+arrows is tedious.
- **Identity:** at icon size you can see a window is there, not what it is.
- **Head-turning:** on an ultrawide the sides are peripheral.

## The model: hunt and return (from the Mac)
Alt+Tab a few times to find something, then a quick Alt+Tab takes you
back; from then on the two windows toggle with a tap. Recency order (most
recently used first) gives this for free.
- Unit: **windows**, not apps (the toggle needs a specific window).
- **Alt+Tab never moves windows.** It only focuses; the chosen window
  gets the focus ring and bounce where it is. Moving is Meta's job.
- All visible windows (one desktop). Glance has no minimized windows
  (parking is its minimize).

## Interaction
- **Tap** (Alt+Tab, released quickly): switch to the previous window
  (ring + bounce).
- **Hold** (Alt still down after `holdDelay`, 200 ms): the **map**.
  - The whole desktop drawn at `mapScale` (50%) in the centre, dimmed
    (`mapDim`): exactly main's width, so the desktop folds into the
    region you're looking at. A map, not a list: the layout stays the
    same (unlike KDE's Overview), so hunting teaches where things live.
  - Overlapping windows spread (below). Shrinking and spreading are one
    movement (`mapTime`, 200 ms).
  - Each Tab moves the selection to the next window in recency order;
    Shift+Tab goes back. The selected window is undimmed and gets the
    ring and bounce. Tab presses during the animation still count.
  - **Label** on the selected window: app icon (40 px) and title (22 px)
    on one line, on a rounded translucent card, centred on the window
    with its bottom on the window's bottom edge (wider than the window if
    need be). Always in the same spot relative to the window, so the eye
    doesn't jump.
  - **The pointer:** hovering a window in the map selects it (undimmed,
    ring and label, no bounce); a left click chooses it at once. The
    label counts as part of its window. Clicks elsewhere, other buttons
    and the wheel do nothing; all pointer input while switching is
    swallowed (a click's release too, even after the map has closed); the
    cursor still moves. A clicked window gets the keyboard back while Alt
    is still held, as in KDE's switcher.
  - Release Alt: the map zooms back; the selected window gets focus in
    place (ring + bounce). Esc: cancel.
  - Panels, notifications and anything else not in the map fade out. The
    wallpaper shrinks with the map; the bands above and below are black.
- **Drawing only**: real frames never move, so apps don't re-lay out and
  the return is exact.

## Spreading piles
Every window must be **countable** and **pointable**, not fully visible;
the label identifies it.
- A **pile** = free (not parked) windows that overlap by more than
  `pileOverlap` (10%) of the smaller one. Stash overlaps and parking
  columns don't count.
- The front window stays in place. The others go, alternately, into a row
  above and a row below the pile, within its width and the space to the
  screen edge, shrunk until the row fits.
- Never sideways: the map's stashes and parking sit left and right, so it
  must stay truthful about left and right.

## Keys and KWin
- Alt+Tab and Meta+Tab (also with Shift; Shift+Tab arrives as
  `Qt::Key_Backtab`). KWin's eight "Walk Through Windows" actions are
  disabled while loaded (`KdeIntegration::disableKdeShortcuts`); they
  belong to the TabBox object, reached via `workspace()->tabbox()` (its
  header isn't installed; it derives from QObject only, so the pointer is
  reinterpreted).
- While switching no surface has keyboard focus
  (`setFocusedKeyboardSurface(nullptr)`, as KDE's switcher does), so apps
  see neither the keys nor a lone Alt (Firefox's menu bar). Focus comes
  back with `input()->keyboard()->update()` in a `singleShot(0)` after
  the releasing key event has gone through.
- Under Alt, Tab and Esc are swallowed (and marked filtered, so their
  releases aren't sent). Under Meta they are passed on (else KDE opens the
  Meta-tap launcher); the disabled actions make them do nothing.
- Recency: Glance keeps its own list (`m_recent`, front on
  `windowActivated`, seeded from the stacking order at load); KWin's
  `FocusChain` isn't exported.
- Meta+Tab is untested here: macOS takes Command+Tab before the VM sees it.

## How it works (the `AltTab` component, kwin/alttab.h/.cpp)
- `key` / `startSwitch` / `step` / `finishSwitch`; the pointer: `motion` /
  `button`, hit-tested with `mapWindowAt` (front to back, where the map
  draws each window this frame; `m_labelRect` first). `m_hold` opens the
  map (`openMap`); `closeMap` animates back from wherever it is.
- `paintWindow`: on top of everything else (parked transforms, bounce),
  the paint data's scale and translation take each window from where it
  is drawn (`currentlyDrawn`) to `mapped(...)`: drawn point = item
  position + translation + scale · item-local point (`createRenderNode`
  in scene/itemrenderer_opengl.cpp), so `t' = to - item + k·(item + t - from)`.
- **Clipping gotcha:** the map sets `PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS`,
  and KWin then gives windows an infinite region; the renderer then clips
  quads in software using only the translation (`clipQuads`), cutting
  shrunk windows off at the right and bottom. Passing a finite region
  (`viewport.deviceRect()`) makes it use GL scissor clipping instead.
  That painting mode ignores damage, so the map redraws the whole screen
  on each of its frames; it asks for frames only while opening or
  closing, and a Tab repaints only with the map up.
- The focus ring goes to `highlighted()` (the selection while switching,
  then the chosen window until it's active); AltTab's `ringChanged` and
  `bounce` signals tell `FocusRing` when to follow or bounce.
- Label: `labelImage` paints icon + title with QPainter into a
  `GLTexture`, drawn in `paintScreen` with the MapTexture|Modulate shader
  (opacity follows the map); redone when the selection or its title
  changes.
- Log lines: `glance: switch started`, `map shown (N windows, M spread)`,
  `window chosen: ...`, `switch cancelled`.
- Testing: `GLANCE_TEST_MAP=1` ([testing.md](testing.md)).

## Maybe later
The selected window's sibling windows lightly undimmed (an app grouping
hint).
