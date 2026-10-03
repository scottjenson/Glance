# Backlog

## Agreed with the user, not built yet
- Crowding of parking columns (should take 10-15); stashes are settled
  (free placement, overlap allowed, Meta+wheel to resize; 2026-10-03).
- Open from the stash talk (2026-10-03): dragging a stashed window that
  was resized by hand within the stash should probably keep its size
  (today the edge rule sets it again). Possible later: Meta+wheel over
  empty parking space sizes all icons at once (dock-size slider).
- Declutter follow-ups: double-clicking two windows to share main; QoL
  tweaks the user noticed but hasn't listed; undo only covers the last
  declutter (offered: undo a whole chain).
- Tuning by feel, as the user uses things: Meta+drag gain/build/slow,
  pause time, snap regions (middle band 20%, parking band 15%); bounce
  frames; preview grow (2x; 2.5x if unreadable).
- Watch the logout hang ([docs/logout-hang.md](../docs/logout-hang.md)).

## Polish
Re-anchor less often (e.g. on press/scroll, or after enough movement);
overlapping parked windows; title-bar buttons of parked windows; text
quality (the Wayfire halving scaler, [docs/wayfire.md](../docs/wayfire.md);
pixel-exact placement at 1x/2x); fractional KDE scales. Earlier
observations: some jank (possibly the re-anchoring on every pointer motion,
or VM load); one unexplained freeze in main that didn't recur.

## Packaging
Not yet: the user wants quality-of-life features first; shipping is the
long-term goal. Fedora COPR / Arch AUR, like other third-party KWin effects
(Better Blur, KDE Rounded Corners). Internal API, so it must be rebuilt per
Plasma release. `README.md` (with install steps) is done; an MIT `LICENSE`
file is not (metadata.json already says MIT). The user hasn't decided
whether to hide their email in commits (GitHub noreply).

## Ideas (not agreed)
Trackpad gestures (discussed 2026-09-30, skipped for now; KWin's input
filters get swipe/pinch/hold events, KDE uses 3- and 4-finger swipes itself;
VMware Fusion only exposes a virtual mouse, so testing would need a USB
Magic Trackpad passed through with `usb.generic.allowHID = "TRUE"`); live
resizing during the drag; fade icon-sized windows or cap them at an icon
size; top/bottom edges; config options; multiple monitors (the on-screen fit
blocks dragging to another one).

## Long term
Ask KWin upstream for a way to set a window's input transform. Fallback
platform if KWin ever fails: a GNOME Shell extension.
