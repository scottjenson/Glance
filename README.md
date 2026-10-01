# Glance

An experiment in window management for wide and ultrawide monitors, built as
an effect for KDE Plasma's window manager (KWin).

On a very wide screen, the middle is where you work and the sides are
awkward. This project treats the edges of the screen as a place to keep
windows you still want at hand: move a window toward either side and it
shrinks, until at the very edge it is small enough to feel like an icon, yet
it stays live and usable.

## How it works

The screen has three kinds of areas:

- **Main** (the center half): windows here are full size.
- **Stash**: between main and the edge. Windows here are shown smaller.
- **Parking**: the very edge. Windows here are icon-sized.

Key mechanics:

- **Dragging a window toward a side shrinks it.** It stays full size while it
  lies in main, then shrinks as its edge moves into the stash, reaching its
  smallest size at the screen edge.
- **Dropping it keeps it there**, exactly as large as it was drawn.
- **Small windows stay usable.** You can click, type and scroll in them. The
  app is really resized to a narrow width, so web pages switch to their
  mobile layout.
- **Hover over an icon-sized window to read it:** it slides out beside its
  column at a readable size. Move along the column to flip through them, or
  move into the preview to use it.
- **Icon-sized windows can be grabbed anywhere:** a drag moves the window,
  and a click still goes to the app.
- **The stash and parking areas keep their windows in a centered stack**,
  which rearranges itself as windows come and go.
- **Dragging a window back into main** restores its original size.
- **Text dropped on the desktop becomes a window:** drag text out of an app
  onto empty desktop, and it is saved as a file in `~/Clips` and opened in
  KWrite right there, so you can keep it at the side like any window. Drop
  it at the very edge of the screen and it goes straight to parking.
- **Meta + C clips the selected text:** highlight text in any window and
  press Meta+C, and it becomes a clip in parking on that window's side.
- **The active window has a thin outline** in your accent color, so you can
  tell which one is selected even when it is small.

## Accelerators

The Meta key (Super / Windows / Command) is the window system's key:

- **Meta + Left / Right** steps the active window between the six places:
  parking, stash, and the halves of main, on either side.
- **Meta + Up** makes it fill the screen height; **Meta + Down** undoes that.
- **Meta + Alt + arrows** selects the nearest window in that direction (the
  same keys as in plain KDE), including small windows at the sides.
- **Meta + double-click** a window to focus on it: it fills the nearer half
  of main, top to bottom, and every other window moves to the sides.
  On the empty desktop it clears main. Do it again to undo.
- **Meta + drag** moves a window from anywhere inside it, and it
  accelerates: keep moving quickly to one side and the window gets ahead of
  the pointer, so it crosses a wide screen with a short movement. Slow
  down or turn back and it follows exactly again. Hold still for a moment
  and it snaps into the half of main, stash or parking it is in.

## Status

Early and experimental. It is developed on Fedora with Plasma 6.7 and uses
KWin's internal API, so it must be rebuilt for each Plasma release. There is
no package yet. The source is in `kwin/`.

## Installing

You build it yourself against your installed Plasma. On Fedora (KDE):

```sh
sudo dnf install git cmake gcc-c++ extra-cmake-modules kwin-devel libepoxy-devel kf6-kglobalaccel-devel
git clone https://github.com/scottjenson/Glance.git
cd Glance
cmake -S kwin -B kwin/build
cmake --build kwin/build
sudo cmake --install kwin/build
```

Then log out and back in. The effect is called **Glance** and can be
turned on and off in System Settings → Window Management → Desktop Effects.
Other distributions need the same things under their own package names
(KWin's development headers, extra-cmake-modules, epoxy).

After each Plasma update, run the last three commands again (a build for the
old version won't load). To uninstall, delete
`/usr/lib64/qt6/plugins/kwin/effects/plugins/glance.so` (the path may
differ on other distributions) and log out and back in.

To try it without installing, `kwin/use-in-session.sh on` loads it from the
build folder at your next login, and `off` undoes that.
