# Focus ring and bounce (built)

## Design
Agreed 2026-09-30, made stronger 2026-10-01: the active window has a 4 px
accent-colored outline, same width on screen at any scale. Whenever a window
gets the ring (any focus change: click, Meta+Alt+arrows, Alt+Tab, new
window; user, 2026-10-02: "any window that gets highlighted for any reason")
it bounces like a pressed button (user's design): frames 100%, 99%, 98%,
99%, 100%, 60 ms apart (`bounceFrames`, `bounceStep`; tried before: 3 frames
to 96% at 150/100 ms, "chunky"; 5 frames to 96%, "too violent"; the user
wants a tiny wiggle), stepped; every step, no movement-vs-release logic.
Tried and rejected the same day: a glow blooming on arrival ("overdone") and
a ring travelling between windows (unclear whether it helped). Dimming
inactive windows was rejected: stashed windows are meant to be used, not
faded (permanent dimming; the Alt+Tab map dims only while Alt is held).
The ring goes to the *highlighted* window: the active one, or during
Alt+Tab the selected one ([alt-tab.md](alt-tab.md)); in the map its
width also allows for the map's scale.

## How it works
`updateRing`, `startBounce`, `paintWindow`: the ring is
a KWin `OutlinedBorderItem` (exported, header installed) as a child of
the active window's `WindowItem`, so it moves/scales/stacks with it; its
width is divided by the item's scale. Color = the app palette's Highlight
(KDE accent); radius = `window->borderRadius()`. Items don't delete their
children and a child must be deleted before its parent, so the ring is
removed on `Window::closed`. All transform changes go through
`setDrawTransform`, which keeps the ring width in step. The bounce is
done in `paintWindow` with the paint data's scale + translation (scale
works around the window item's origin: `renderItem` applies it after
`item->position()`; so translate by the item-local center·(1−s)), not with
the item transform, so it doesn't touch our own drawing; two
`QTimer::singleShot`s (counter guards stale ones); `isActive()` is true
while bouncing.
