# Focus ring and bounce

## Design
The highlighted window has a 4 px (`ringWidth`) outline in the accent
color, the same width on screen at any scale. Highlighted = the active
window, or during Alt+Tab the selected one ([alt-tab.md](alt-tab.md)).
Inactive windows are never dimmed: stashed windows are meant to be used.

When the ring moves by keyboard (Meta+Alt+arrows, Alt+Tab, the window
chosen in the Alt+Tab map) the window bounces like a pressed button: a
tiny stepped wiggle, scales `bounceFrames` (100, 99, 98, 99, 100%)
`bounceStep` (60 ms) apart. Only for keyboard moves: with the mouse the
user is already looking at the window, and bouncing on every focus change
(clicks, drags, apps taking focus) was busy.

## How it works
The `FocusRing` component (kwin/focusring.h/.cpp: `update`,
`startBounce`, `paintWindow`).
- The ring is a KWin `OutlinedBorderItem` (exported, header installed), a
  child of the window's `WindowItem`, so it moves, scales and stacks with
  it; its width is divided by the item's scale (and the map's scale,
  `mapZoom`). All transform changes go through `setDrawTransform`, which
  keeps the width in step. Color = the palette's Highlight (KDE accent);
  radius = `window->borderRadius()`.
- Items don't delete their children and a child must be deleted before
  its parent, so the ring is removed on `Window::closed`.
- The bounce is done in `paintWindow` with the paint data's scale and
  translation, not the item transform, so it doesn't touch Glance's own
  drawing. The scale works around the window item's origin (`renderItem`
  applies it after `item->position()`), so translate by the item-local
  center·(1−s). Two `QTimer::singleShot`s, a counter guards stale ones;
  `isActive()` is true while bouncing.
- Tilted parking tiles get the ring inside their mipmap image
  ([dragging-and-parking.md](dragging-and-parking.md)).
