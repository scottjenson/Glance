# Keyboard: Meta+arrows, Meta+Alt+arrows, the Meta tap

Replaces KDE's quick tiling on Meta+arrows; acts on the active window.

## Meta+Left/Right: the ladder
parking L ← stash L ← left half of main ← right half of main → stash R →
parking R, always one step in the arrow's direction. Meta+Left/Right never
resize a window in main:
- In main a window keeps its size and vertical position and is centered
  in a half, kept inside main (`nextMainStop`/`mainStopRect` in
  kwin/geometry.cpp). A window wider than a half sits against main's
  edge; one as wide as main has a single stop. At the last stop on that
  side it goes on into the stash.
- Back from a stash it gets its original size, in the half on that side.
- Into a stash: `stashScale` (50%), at most `stashMaxWidth` (60%) of the
  zone wide, at least `stashMinSize` (1.5 x parking's 180 px) on the
  longer side or nearly full size if smaller, centered in the stash zone
  (may overlap others). Into parking: a tile against the screen edge.

## Meta+Up/Down: two views
Always full height, no toggles; neither acts on parked windows.
- Meta+Up: the half view, a half of main at full height. A window stays
  in its half; from all of main it takes the free half if exactly one is
  free, else left (`Keyboard::freeHalf`); a free window the nearer half.
- Meta+Down: the full view, all of main (`Place::Full`) at full height.
- "In a half" (or in Full) = the window's horizontal extent and the
  area's share ≥ 80% (IoU, `halfMatch`).

Keyboard moves animate (`animationTime`, 180 ms, ease-out); the app
resizes during the glide. The app re-laying out still flashes a little;
a fix would be snapshot + cross-fade (KWin's CrossFadeEffect, as KDE's
maximize animation does).

## Meta+Alt+arrows: selecting
Meta moves, Meta+Alt selects (the same keys as in plain KDE). Activates
the nearest window in that direction by *drawn* position (centers, KWin's
`switchWindow` scoring). KDE's own "Switch Window Left/Right/Up/Down"
actions use real frames, wrong for parked windows.

## KDE's shortcuts and the Meta tap
- Glance's filter must not swallow Meta+arrows: KDE's shortcut system
  would then think Meta was tapped alone and open the launcher. So the
  keys are passed on, and KWin's conflicting QActions (the four "Window
  Quick Tile ..." and four "Switch Window ..." actions, children of
  Workspace) are disabled while loaded (`KdeIntegration`, kwin/kde.cpp).
  Quick tiling by dragging (`options->electricBorderTiling`) is turned
  off too. Everything is restored on unload.
- KDE opens its launcher when Meta is pressed and released with nothing
  in between. KWin calls the tap off on a click or scroll, but in a filter
  that runs after Glance's, and VMware Fusion sometimes sends Command as an
  instant press+release. So only a real tap opens it
  (`KdeIntegration::metaKey`, `cancelMetaTap`): a click or scroll Glance
  takes with a modifier held calls it off, and a press held longer than
  `metaTapMax` (400 ms) or shorter than `metaTapMin` (10 ms) doesn't
  count.
- `cancelMetaTap` invokes the `cancelModiferOnlySequence` slot (sic) on
  KWin's kglobalaccel plugin, a static Qt plugin found through
  `QPluginLoader::staticPlugins()` (not exported otherwise). A warning is
  logged at load if it isn't found.
