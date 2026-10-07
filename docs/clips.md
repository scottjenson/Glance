# Clips

Text or an image dragged onto the desktop, or selected text clipped with
Meta+C, becomes a clip: a small sticky-note window (glance-clip) that can
sit in a stash or parking like any window and drags back into documents.
The start of "erase the lines between windows, files and the clipboard".

## Making a clip (the `Clips` component, kwin/clips.h/.cpp)
- **Drop on the desktop** (`drop`, `readClip`, `finishClip`, `placeClip`):
  a drag released over the desktop (or nothing) would otherwise become a
  Plasma sticky-note widget, which is not a window. Glance's filter runs
  before KWin's DragAndDrop filter: it requests the data from the drag
  source into a pipe (`AbstractDataSource::requestData`, non-blocking
  with a `QSocketNotifier`), holds the release back, and when the data is
  in (or after `clipTimeout`, 2 s; images `clipImageTimeout`, 6 s) cancels
  the drag, passes the release on, saves the clip to `~/Clips/<date
  time>.txt` (or an image file) and starts glance-clip on it. Drops on
  windows are left alone, except on parking icons in the parking band.
- **Placing:** the clip window (matched by pid) is placed as if dragged
  there held at its center and dropped (edge rule, parked if shrunk).
  Within `parkingBand` (15% of the edge zone) of a screen edge it becomes
  a parking icon in that column.
- **Images:** a drop asks for image data before text (`clipMimeType`:
  image/png first, then any type Qt reads; Firefox's image drags carry
  the pixels). A drag with only `text/uri-list` is read first
  (`droppedImageFile`): a single local image file (Dolphin, Spectacle) is
  copied into ~/Clips; anything else (files, links) goes to Plasma as
  usual.
- **Meta+C** (`clipSelection`): a KGlobalAccel shortcut ("Glance Clip
  Selection", in System Settings under KWin, rebindable). Clips the
  primary selection if its source is the active window's client, into
  parking on the side nearer that window. Text only. Drops and Meta+C
  share `startClip`/`finishClip` (`ClipRead::fromDrag` says whether a
  drag must be cancelled).
- **Limits:** reading gives up past `clipMaxBytes` (50 MB, a log line;
  the drop then does nothing); images are checked by their header only
  (`QImageReader` on a `QBuffer`); the file is written, or copied, off
  KWin's main thread (`QtConcurrent::run`), and glance-clip starts when it
  is saved.
- glance-clip is started with KWin's startup environment minus
  QT_PLUGIN_PATH, via `systemd-run --user --scope --slice=app.slice` (its
  own app scope like Plasma-started apps, not part of KWin's service; the
  scope execs the app, so the pid stays the app's).
- `kwin/kwin-private/wayland/abstract_data_source.h` is copied from KWin
  6.7.5 (Fedora's kwin-devel doesn't install it; libkwin exports the
  class).

## glance-clip, the clip app (kwin/clip/main.cpp)
A small Qt app (KWrite only drags a selection, Klipper and CopyQ are one
list window, Plasma's sticky note is a widget), built with the effect into
`kwin/build/bin/glance-clip`. Glance looks there first (three folders up
from its plugin, via `dladdr`), else on PATH. App id `org.glance.Clip`
(`isClip`); `kwin/clip/org.glance.Clip.desktop` gives KWin the window icon
(installed by CMake; copied by hand to ~/.local/share/applications on
this VM).
- Text: read-only, large (1.4x the desktop font), no menus or scroll bars
  (the wheel scrolls), Ctrl+C copies. Starts 480 px wide, as tall as the
  text; narrower, it picks its own height (`resizeEvent`, up to 480) and
  Glance takes it (`Clips::frameChanged`).
- Images (`ImageView`): the image fills the window under the title bar,
  starting at its own size scaled down to fit 480x480; made wider or
  narrower it keeps the image's proportions. Ctrl+C copies the image.
- Sticky-note look: yellow body and title bar (a KDE color scheme written
  to ~/.cache/glance-clip/sticky-note.colors and set as
  `KDE_COLOR_SCHEME_PATH`, which the Plasma platform theme hands to KWin's
  title bar) and no title (Qt needs an empty display name too), so
  Alt+Tab's label shows only the icon.
- Closing the window deletes the clip file (not at logout:
  `commitDataRequest`). Clips aren't restored after a login.

## Dragging a clip back out
The user's model: a physical object. The body is a drag source (no
selecting first): a real drag and drop, since only that lets an app say
it takes the data (Wayland hides whether a spot is a text field). It
looks like moving the note: Glance draws the clip under the pointer
(`dragStarted`, `ghostRect`: held where grabbed, scaled by the edge rule,
kept on screen), the app's drag picture is a transparent pixel, and the
window takes no input meanwhile (a 1-px `QWindow::setMask`), so drops
near its old place fall through. Where it lands decides (`dragEnded`,
`placeDroppedClip`):
- an app that takes it (`SeatInterface::dragDropped`): pasted, and the
  clip closes (a move); with Shift held it is copied and the note glides
  back. The clip offers only CopyAction; the app decides.
- the desktop or nothing: the note moves there like a dropped window; in
  the parking band it joins that column.
- anything else: it glides back.

Images are offered as PNG data, Qt's image and the file (file://, for
file managers and upload forms); when moved, the window closes at once
but the file is deleted a minute later (Dolphin asks "copy here?" before
reading it). Title bar and Meta+drag still move clips plainly. The ghost
needs the whole screen painted as transformed, with finite regions per
window (the clipQuads gotcha, [alt-tab.md](alt-tab.md)).

## Sizes
- In a stash a clip keeps its full width, only drawn smaller, like any
  window; its height is what its text needs (KWin reports a 150 px
  minimum, which made one-word clips square).
- In parking a clip keeps its own shape (not a square tile) at tile size,
  laid out at twice its drawn size and drawn at 1/2. Hover previews grow
  it to at most 1:1.
