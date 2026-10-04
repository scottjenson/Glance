# Keyboard: Meta+arrows and Meta+Alt+arrows (built)

## Meta+arrows (phase 1)
Agreed 2026-09-29, may evolve; replaces KDE's quick tiling on Meta+arrows,
intercepted by our input filter, active window only.

- Meta+Left from a free window in main: snap to the left half of main
  (x = main's left edge, width = half of main; height and vertical
  position unchanged; real resize). Again: 50% size in the left stash.
  Again: left parking (15%). Meta+Right mirrors and walks back
  (parking L → stash L → left half → right half → stash R → parking R).
- Meta+Up / Meta+Down (decided 2026-10-01, "people who don't want two
  halves"; the idea: use windows at full height in main, then shrink
  them to the sides): two fixed views, always full height, no toggles.
  Meta+Up = the half view: a half of main at full height (stays in its
  half; from all of main the free half if exactly one is free, else left,
  "when in doubt go left", `freeHalf`; a free window the nearer half).
  Meta+Down = the full view: all of main (`Place::Full`, same 80% IoU
  test as halves) at full height. Neither acts on parked windows.
  Meta+Left/Right from all of main go straight to the stash on that side
  and back into all of main (`m_wasFull`); half windows walk the ladder
  as before. Wide windows in a stash are capped at `stashMaxWidth` (60%)
  of the zone (they may overlap neighbours; accepted for now). Placed
  stash windows are centered in the stash zone (2026-10-02, user's idea;
  before, the outer edge sat where a drag gives that scale, which left
  ~40% of the zone empty outside and misaligned the column); parking
  icons sit against the screen edge. Earlier
  the same day: Meta+Up as a height toggle (full <-> half height) was a
  misreading of "half of the main center area" (= half width). KDE's
  Meta+PgUp (maximize) was considered and dropped by the user (hard to
  press, fills the stashes too).
- "In a half" = the window's horizontal extent and the half's share
  >= 80% (IoU, `halfMatch`).
- Keyboard moves animate (180 ms, ease-out); the app resizes during the
  glide. A flash from the app re-laying out remains (noted by the user
  as spoiling the effect a bit); fix if wanted: snapshot + cross-fade
  (KWin's CrossFadeEffect, as KDE's maximize animation does).
- KDE's Meta-tap launcher: our filter must NOT swallow Meta+arrows (KDE's
  shortcut system then thinks Meta was tapped alone and opens the
  launcher). Instead the keys are passed on and KWin's four "Window
  Quick Tile Left/Right/Top/Bottom" QActions (children of Workspace) are
  disabled while the effect is loaded. `cancelModiferOnlySequence` is
  not exported to plugins.
- Quick tiling by dragging (`options->electricBorderTiling`) is turned off
  while loaded and restored on unload.

## Selecting, Meta+Alt+arrows
Agreed 2026-09-30; "our one nod to compatibility with KDE": Meta moves,
Meta+Alt selects. Activates the nearest window in that direction by *drawn*
position (centers, KWin's `switchWindow` scoring). KDE's own "Switch Window
Left/Right/Up/Down" actions are disabled while loaded (they use real frames,
wrong for parked windows); keys passed on, as for Meta+arrows.

## Meta alone and KDE's launcher (2026-10-03)
KDE opens its launcher when Meta is pressed and released with nothing in
between. The user saw it open by itself mid-demo. Two causes: KWin calls
the tap off when it sees a click or scroll during the press, but in its
GlobalShortcut filter, which runs after ours, so a Meta+wheel or Meta
click Glance takes left the tap standing; and VMware Fusion sometimes
sends Command as an instant press+release (see CLAUDE.md). Now only a
real tap opens the launcher (`metaKey`, `cancelMetaTap`):
- a click or scroll Glance takes with a modifier held calls it off
  (`Filter::pointerButton`/`pointerAxis`);
- a press held longer than `metaTapMax` (400 ms) doesn't count (you meant
  something else and let go), nor one shorter than `metaTapMin` (10 ms,
  no hand is that fast: VMware's taps).
KWin doesn't export its call for this; `cancelMetaTap` invokes the
`cancelModiferOnlySequence` slot (sic) on KWin's kglobalaccel plugin, a
static Qt plugin found through `QPluginLoader::staticPlugins()`. A warning
is logged at load if it isn't found.
