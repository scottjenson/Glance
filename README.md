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
- **Stash** (the "stash"): between the middle and the edge. Windows here
  are shown smaller.
- **Parking**: the very edge. Windows here are icon-sized.

Key mechanics:

- **Dragging a window toward a side shrinks it.** It stays full size while it
  lies in the middle, then shrinks as its edge moves into the stash area,
  reaching its smallest size at the screen edge.
- **Dropping it keeps it there**, exactly as large as it was drawn.
- **Small windows stay usable.** You can click, type and scroll in them. The
  app is really resized to a narrow width, so web pages switch to their
  mobile layout.
- **Icon-sized windows can be grabbed anywhere:** a drag moves the window,
  and a click still goes to the app.
- **Staging and the parking lot keep their windows in a centered stack**,
  which rearranges itself as windows come and go.
- **Dragging a window back into the middle** restores its original size.

## Accelerators

The Meta key (Super / Windows / Command) is the window system's key:

- **Meta + Left / Right** steps the active window between the six places:
  parking lot, staging, and the halves of the middle, on either side.
- **Meta + Up** makes it fill the screen height; **Meta + Down** undoes that.
- **Meta + drag** does the same with the mouse: drag sideways and the window
  snaps from place to place as you go. Drag up for full height, or
  diagonally for the half of the middle on that side. Let go of the mouse
  to keep it there, or let go of Meta to go back to a normal drag.

## Status

Early and experimental. It is developed on Fedora with Plasma 6.7 and uses
KWin's internal API, so it must be rebuilt for each Plasma release. There is
no package yet. The source is in `kwin/`.

## Installing

You build it yourself against your installed Plasma. On Fedora (KDE):

```sh
sudo dnf install git cmake gcc-c++ extra-cmake-modules kwin-devel libepoxy-devel
git clone https://github.com/scottjenson/WideMonitorUX.git
cd WideMonitorUX
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
