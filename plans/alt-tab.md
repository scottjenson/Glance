# Plan: Alt+Tab, hunt and return (active; designed 2026-10-02, not built)

## Why Glance needs it
Glance's side windows are visible and usable in place (type, sort, play or
pause music without bringing them to main), so the classic job of Alt+Tab,
showing windows buried behind each other, mostly falls away. What Glance
lacks is:
- **Time:** everything is spatial (click, Meta+Alt+arrows); nothing says
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
  the focus ring and bounce where it is. Moving is Meta's job (Meta+arrows
  bring it into main).
- Windows: all visible windows (one desktop). Minimized windows: Glance
  has none by design (parking is its minimize); see the backlog item on
  the minimize button.

## Interaction (phase 1)
- **Tap** (Alt+Tab, released quickly): switch to the previous window.
  Nothing else visible happens: ring + bounce.
- **Hold** (Alt still down after ~200 ms): the **map** appears.
  - Stage 1, shrink (~150 ms): the whole desktop is drawn at 50% in the
    centre of the screen, dimmed. 50% of the width is exactly main's
    width: the whole desktop folds into the region you're looking at, in
    your cone of vision. It's a map, not a list: the layout stays the
    same (unlike KDE's Overview or Mission Control, which rearrange into a
    grid), so hunting still teaches where things live.
  - Stage 2, spread (~150 ms): overlapping windows fan out (below).
  - Each Tab moves the highlight to the next window in recency order;
    Shift+Tab goes back. The selected window is drawn undimmed. Tab
    presses during the animations still count.
  - **Label**: always in the same spot, the centre of the screen: the
    app's icon, large (~128 px), the window title below it in a big,
    easy-to-read font, on a rounded semi-transparent card. Semantic cues
    (icon + title), not zooming the window, say what it is; the highlight
    in the map says where.
  - Release Alt: the map zooms back to full size; the selected window gets
    focus in place (ring + bounce). Esc: cancel, back as before.
- **Drawing only**: the map, the dimming and the spread change only how
  windows are drawn (the same technique as parked windows); real frames
  never move, so apps don't re-lay out (no flashing) and the return is
  exact.

## Spreading piles
Goal (user, 2026-10-02): every window **countable** (you can see there are
four) and **pointable** (the highlight lands on something distinct), not
fully visible; the label identifies it.

- A **pile** = windows that overlap meaningfully (more than ~10% of the
  smaller one). Stash neighbours' slight overlaps (`stashMaxWidth`) don't
  count; parking columns don't overlap. In practice piles are in main.
- The front window of a pile stays in place. The others go into a row
  above and a row below the pile (alternating), within the pile's own
  horizontal footprint, shrunk until the row fits the pile's width and the
  free band (a quarter of the screen height above and below the map) -
  like temporary vertical stashes (the user's idea).
- Arithmetic for full-height windows in main at a 50% map: shrunk by
  another half, one fits above exactly and two fit side by side; so front
  + 2 above + 2 below = 5. More: the rows shrink further.
- Pushing sideways was considered and rejected for now: the map's own
  stashes and parking sit left and right, so a main window pushed sideways
  would look parked; the map must stay truthful about left and right.
  An earlier idea (push up/down and repeat until nothing overlaps) is
  replaced by the rows, which need no loop and always end.

## Keys and KWin
- Alt+Tab and Meta+Tab (KDE binds both to "Walk Through Windows"; Alt
  normally belongs to apps, Alt+Tab is the universal exception).
- Full takeover (option 3, chosen by the user): our input filter
  (ButtonRebind order, before KWin's tab box) handles Tab, Shift+Tab, Esc
  and the modifier release; KWin's tab box actions are disabled while
  loaded. They are KGlobalAccel QActions created by `TabBox::key()` in
  tabbox/tabbox.cpp, children of the TabBox object, objectName = the
  untranslated name: "Walk Through Windows", "... (Reverse)", "...
  Alternative", "... Alternative (Reverse)", and the "... of Current
  Application" variants (Alt+\`, Meta+\`).
- Pass the keys on (as with Meta+arrows) or check what swallowing them
  does to KDE's Meta-tap launcher (Meta+Tab).
- Recency: KWin's `FocusChain` (`workspace()->focusChain()`, header
  installed at /usr/include/kwin/focuschain.h).
- The effect API also still has the tab box takeover hooks (`refTabBox`,
  `tabBoxAdded`, ...; option 2), not used: no other KWin 6 effect uses
  them, and we want to own the keys.
- Rendering: per-window draw transforms as for parked windows
  (`setDrawTransform`; the focus ring follows), dimming through paint
  data; panels and wallpaper to be decided while building. KWin's Zoom
  effect renders the whole screen scaled, if we need that instead.

## Phase 2 (agreed to wait until phase 1 shows whether it works)
- Mouse in the map: click a window to choose it.
- App grouping hint: the selected window's sibling windows lightly
  undimmed.
- Tuning by feel: map scale (50%), dim level, hold delay (~200 ms),
  animation times, pile threshold.
