# Clips (built 2026-09-30)

The start of "erase the lines between windows, files and the clipboard".
Clips are glance-clip windows (sticky notes) whose text drags back into
documents (below). Image clips: see "Image clips" below (built
2026-10-03). Rich text is planned:
[plans/clips-back.md](../plans/clips-back.md).

The `Clips` component (`kwin/clips.h/.cpp`): `drop`, `readClip`,
`finishClip`, `placeClip`: a text drag released
over the desktop (or nothing) would become a Plasma sticky-note widget,
which is not a window. Our filter runs before KWin's DragAndDrop filter: it
requests the text from the drag source into a pipe
(`AbstractDataSource::requestData`), holds the release back, and when the
data is in (or after `clipTimeout`) cancels the drag, passes the release on,
saves the text to `~/Clips/<date time>.txt` (on a worker thread,
`saveClip`) and then starts `glance-clip <file>` with
KWin's startup environment minus QT_PLUGIN_PATH, via
`systemd-run --user --scope --slice=app.slice` (its own app scope like
Plasma-started apps, not part of KWin's service; the scope execs the app,
so the pid stays the app's). Until 2026-10-03 clips opened in KWrite. The window whose pid matches is placed as if
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

Limits (code review finding 4, 2026-10-04): reading gives up past
`clipMaxBytes` (50 MB, a log line; the drop then does nothing), images are
checked by their header only (`QImageReader` on a `QBuffer`, no full
decode), and the file is written, or a dropped image file copied, off
KWin's main thread (`QtConcurrent::run`); glance-clip starts when it is
saved.

`kwin/setup-kwrite.sh [size] [font]` set KWrite's editor font when clips
were KWrite windows (no longer needed).

`kwin/kwin-private/` holds `wayland/abstract_data_source.h` (copied from
KWin 6.7.5; Fedora's kwin-devel doesn't install it, libkwin exports the
class). Keep it in step with the installed KWin.

## Image clips (built 2026-10-03)
Agreed with the user 2026-10-03. A drop on the desktop asks for image data
before text (`clipMimeType` with `images`: image/png first, then any image
type Qt reads). Firefox's image drags carry the pixels (image/png, jpeg,
gif, plus its link, text/uri-list, file promises; checked in the log, which
shows each desktop drop's types), at the best resolution it loaded. Other
apps' image drags work the same. A drag with only text/uri-list is read
first (`droppedImageFile`): a single local image file (Dolphin, Spectacle)
is copied into ~/Clips; anything else is dropped on Plasma after all
(`finishClip` passes the release on without cancelling the drag). Links
without image data stay Plasma's. Images get `clipImageTimeout` (6 s; the
app may encode first). Meta+C stays text only (the user left the
clipboard-image fallback out).

glance-clip shows an image file as an image clip (`ImageView`): the image
fills the window under the yellow title bar, starting at its own size
(pixels taken as logical px) scaled down to fit 480x480. Made wider or
narrower (Glance, parking) it takes the image's proportions again; made
only taller or shorter by hand it keeps that. Dragged out it offers PNG
data, Qt's image and the file (file://, for file managers and upload
forms); moved, the window closes at once but the file goes a minute later
(Dolphin asks "copy here?" before reading it). Ctrl+C copies the image.

Possibly related: the plasmashell logout hang,
[docs/logout-hang.md](logout-hang.md).

## glance-clip: the clip app (built 2026-10-03)
Agreed with the user 2026-10-03. KWrite only drags a selection, Klipper and
CopyQ are one list window, Plasma's sticky note is a widget; so our own
small Qt app, `kwin/clip/main.cpp`, built with the effect into
`kwin/build/bin/glance-clip` (Glance looks there first, three folders up
from its plugin, via `dladdr`; else PATH). App id `org.glance.Clip`
(`isClip` in Glance); `kwin/clip/org.glance.Clip.desktop` gives KWin the
window icon (Klipper's; installed by CMake, copied by hand to
~/.local/share/applications on this VM).
- Read-only, large text (1.4x the desktop font), no menus, no scroll bars
  (the wheel scrolls), Ctrl+C copies. Starts 480 px wide, as tall as the
  text (3 lines to 480 px).
- Sticky-note look (user, 2026-10-03): yellow body and title bar (a KDE
  color scheme written to ~/.cache/glance-clip/sticky-note.colors and set
  as `KDE_COLOR_SCHEME_PATH`, which the Plasma platform theme hands to
  KWin's title bar), and no title (Qt needs an empty display name too).
  So Alt+Tab's label shows only the icon for clips.
- Closing the window deletes the clip file (not at logout:
  `commitDataRequest`). Clips aren't restored after login (open question).

### Dragging a clip (user's model: a physical object)
The body is a drag source (no selecting first): a real drag and drop of
the text, since only that lets an app say it takes text (Wayland hides
whether a spot is a text field; a fake click + Ctrl+V was ruled out). It
looks like moving the note: Glance draws the clip under the pointer
(`dragStarted`, `ghostRect`: held where grabbed, scaled by the
edge rule, kept on screen), the app's drag picture is a transparent pixel,
and the window takes no input meanwhile (a 1-px `QWindow::setMask`), so
drops near its old place fall through. Where it lands decides
(`dragEnded`, `placeDroppedClip`):
- an app that takes it (`SeatInterface::dragDropped`): pasted, and the app
  closes the clip (move); Shift held at the drop: copied, the note glides
  back. The app offers only CopyAction and decides itself.
- the desktop or nothing: the note moves there like a dropped window
  (parked if shrunk); in the parking band it joins that column at the edge.
  (First built as "cancel, it stays"; changed the same day.)
- anything else: it glides back.
Clips are exempt from the tiny-icon press handling (`holdPress`): an app
can only start a drag from a press it received. During any drag and drop
Glance doesn't re-anchor parked windows (`route`). Title bar and Meta+drag
still move clips plainly. The ghost needs the whole screen painted as
transformed, with finite regions per window (the clipQuads gotcha, see
[alt-tab.md](alt-tab.md)).

### Sizes in parking
- Parking icons are at least `parkingMinWidth` (180 px) wide, for any
  window (`parkingScale`; clips were 72 px specks at 15% of 480).
- Clips in parking are laid out at twice their drawn width and drawn at
  1/2, like a stash clip, so text is the same size in both (user: the
  1:1 version was too big next to the stash; the 400-px layout before that
  made 3-px text). They count as icons, so hover previews grow them 2x,
  to 1:1. The app picks its own height when narrower than it started
  (`resizeEvent`: as tall as its text, up to 480), and Glance takes it
  (`frameChanged`) and re-forms the column (a fixed 150-px minimum
  made one-word clips square; user, 2026-10-03).
