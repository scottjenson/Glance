# Backlog

Next is [remembered-places.md](remembered-places.md). Everything here is
for later.

## Kept for later
- **Rich-text clips:** ask the source for text/html (Firefox offers it)
  or RTF, store it, offer the same formats when dragged back out (Qt
  shows simple HTML). Main work: cleaning web HTML down to
  bold/italic/links/lists/headings; images in web HTML are often remote
  links. Also left out: https-only image drags (would need a download).
- **Clips after a login:** the files stay in ~/Clips but the windows
  aren't reopened (could come with remembered places).

## Known limits
- Parked windows' invisible full-size frames can catch drag-and-drop
  drops meant for windows around them (KWin's DnD goes by frames).
- More parking tiles than fit run past the column's ends.
- Multi-monitor is largely untested: the Alt+Tab map opens on
  `activeOutput()`, switching checks only the window's own output, and
  the on-screen fit blocks dragging to another monitor.
- If KWin crashes or restarts, parked apps keep their small layout sizes
  (remembered places will save state to a file).
- Relies on KWin internals (private headers, `cancelModiferOnlySequence`
  by name, the TabBox cast): check them on every Plasma release.

## Watch
- KDE's launcher opening by itself (fixed as far as known,
  [docs/keyboard.md](../docs/keyboard.md)). If it recurs, log Meta key
  timings again.

## Polish
Overlapping parked windows; title-bar buttons of parked windows; sharp
text in the stash (a lower render scale for the app,
`Window::setNextTargetScale`); fractional KDE scales; the flash when an
app re-lays out during a keyboard move (cross-fade); Meta+wheel scroll
speed and natural-scrolling direction; the parking-crowding design (10-15
icons per column is fine for now).

## Packaging
After quality-of-life features. Fedora COPR / Arch AUR, like other
third-party KWin effects (Better Blur, KDE Rounded Corners). Uses KWin's
internal API, so it must be rebuilt per Plasma release. README (install
steps), LICENSE and the version check are done. Open: whether to hide
the user's email in commits (GitHub noreply).

## Ideas (not agreed)
Trackpad gestures (KWin's input filters get swipe/pinch/hold; KDE uses
3- and 4-finger swipes itself; testing in the VM needs a USB Magic
Trackpad passed through with `usb.generic.allowHID = "TRUE"`); live
resizing during the drag; top/bottom edges; config options; multiple
monitors.

## Long term
Ask KWin upstream for a way to set a window's input transform (would
replace re-anchoring). Fallback platform if KWin ever fails: a GNOME
Shell extension.
