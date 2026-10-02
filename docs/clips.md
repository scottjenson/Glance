# Clips (built 2026-09-30)

The start of "erase the lines between windows, files and the clipboard".
Getting clips back into documents is planned:
[plans/clips-back.md](../plans/clips-back.md).

`dropToClip`, `readClip`, `finishClip`, `placeClip`: a text drag released
over the desktop (or nothing) would become a Plasma sticky-note widget,
which is not a window. Our filter runs before KWin's DragAndDrop filter: it
requests the text from the drag source into a pipe
(`AbstractDataSource::requestData`), holds the release back, and when the
data is in (or after `clipTimeout`) cancels the drag, passes the release on,
saves the text to `~/Clips/<date time>.txt` and starts `kwrite <file>` with
KWin's startup environment minus QT_PLUGIN_PATH, via
`systemd-run --user --scope --slice=app.slice` (its own app scope like
Plasma-started apps, not part of KWin's service; the scope execs KWrite, so
the pid stays KWrite's). The window whose pid matches is placed as if
dragged there held at its center and dropped (edge rule, parked if shrunk).
Drags with `text/uri-list` (files, links) and drops on windows are left
alone, except on parking icons in the parking band.

Parking band (built 2026-10-01, the user asked for this snap explicitly):
text dropped within `clipParkingBand` (0.15 of the edge zone, ~150 px) of a
screen edge becomes a parking icon in that column (`commitPlace`,
`m_clipPlace`).

Meta+C (`clipSelection`, built 2026-10-01; Meta+Left/Right was rejected
for this: it already moves windows, and the primary selection outlives the
highlight): a KGlobalAccel shortcut ("Glance Clip Selection", listed in
System Settings under KWin, rebindable). Clips the primary selection if
its source's client is the active window's client, into parking on the
side nearer that window. Drops and Meta+C share `startClip`/`finishClip`
(`ClipRead::fromDrag` says whether a drag must be cancelled).

`kwin/setup-kwrite.sh [size] [font]` sets KWrite's editor font for clips
(~/.config/kwriterc, "KTextEditor Renderer"/"Text Font"; default Noto Sans
Mono 16); run once on this VM (2026-09-30).

`kwin/kwin-private/` holds `wayland/abstract_data_source.h` (copied from
KWin 6.7.5; Fedora's kwin-devel doesn't install it, libkwin exports the
class). Keep it in step with the installed KWin.

Possibly related: the plasmashell logout hang,
[docs/logout-hang.md](logout-hang.md).
