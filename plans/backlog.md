# Backlog

## Order agreed with the user (2026-10-04)
1. Alt+Tab phase 2: click a window in the map to choose it
   ([docs/alt-tab.md](../docs/alt-tab.md)). Built and checked 2026-10-04.
2. Packaging, first steps: an MIT `LICENSE` file and the version check
   (a dialog at login when KWin didn't load Glance; the build stops on
   another Plasma release), both built 2026-10-04
   ([docs/version-check.md](../docs/version-check.md)).

## Later (kept, not now)
- Clips: rich text ([clips-back.md](clips-back.md)); clips aren't
  restored after a login.

## Watch
- The logout hang ([docs/logout-hang.md](../docs/logout-hang.md)).
- KDE's launcher opening by itself (fixed 2026-10-03 as far as
  known, [docs/keyboard.md](../docs/keyboard.md); it happened once more,
  maybe a real Meta tap). If it recurs, log Meta key timings again.

Dropped 2026-10-04 (user: good enough for testing, revisit only if play
shows a need): crowding of parking columns (10-15 is fine), Meta+wheel
over empty parking to size all icons, declutter follow-ups, tuning by
feel, Meta+C with a clipboard image.

## Polish
Re-anchor less often (e.g. on press/scroll, or after enough movement;
now finding 1 of [code-review.md](code-review.md), with a multi-monitor bug);
overlapping parked windows; title-bar buttons of parked windows; text
quality (the Wayfire halving scaler, [docs/wayfire.md](../docs/wayfire.md);
pixel-exact placement at 1x/2x); fractional KDE scales. Earlier
observations: some jank (possibly the re-anchoring on every pointer motion,
or VM load); one unexplained freeze in main that didn't recur.

## Packaging
Not yet: the user wants quality-of-life features first; shipping is the
long-term goal. Fedora COPR / Arch AUR, like other third-party KWin effects
(Better Blur, KDE Rounded Corners). Internal API, so it must be rebuilt per
Plasma release. `README.md` (with install steps) is done; the `LICENSE`
file and the KWin version check are done (see the order above). The user hasn't
decided whether to hide their email in commits (GitHub noreply).

## Ideas (not agreed)
Trackpad gestures (discussed 2026-09-30, skipped for now; KWin's input
filters get swipe/pinch/hold events, KDE uses 3- and 4-finger swipes itself;
VMware Fusion only exposes a virtual mouse, so testing would need a USB
Magic Trackpad passed through with `usb.generic.allowHID = "TRUE"`); live
resizing during the drag; fade icon-sized windows or cap them at an icon
size; top/bottom edges; config options; multiple monitors (the on-screen fit
blocks dragging to another one). Apps remember their parked size across a
logout (found 2026-10-04: Firefox reopens at its 500 px minimum if it was
parked at logout, and that becomes its "original" size; dragged back to
main it stays 500 px): maybe give parked windows their full size back at
logout, or remember originals by app.

## Long term
Ask KWin upstream for a way to set a window's input transform. Fallback
platform if KWin ever fails: a GNOME Shell extension.
