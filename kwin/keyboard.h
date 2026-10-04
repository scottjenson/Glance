// Keyboard (docs/keyboard.md; replaces KDE's quick tiling on Meta+arrows).
// Meta+Left/Right step the active window between left parking, left stash,
// left half of main, right half of main, right stash and right parking; a
// free window in main first snaps to the half on that side. A window in all
// of main (see Meta+Down) goes straight to the stash on that side, and
// comes back as wide (see stepSideways). Windows move horizontally and
// keep their vertical position. Meta+Up: the half view, a half of main at
// full height; Meta+Down: the full view, all of main at full height.
// Keyboard moves animate (the drawing glides to the new place while the
// app resizes).
//
// Selecting (Meta+Alt+arrows, KDE's own keys for this): activates the
// nearest window in that direction, judged by where windows are drawn
// (KDE's version uses the real frames, which are wrong for parked
// windows); the focus ring bounces there.
#pragma once

#include "parked.h"

#include <QObject>

#include <set>

namespace KWin
{
struct KeyboardKeyEvent;
}

namespace glance
{

class FocusRing;

class Keyboard : public QObject
{
    Q_OBJECT

public:
    Keyboard(ParkedWindows &parking, FocusRing &focusRing);

    // Every key not taken before (Alt+Tab): Meta+arrows and
    // Meta+Alt+arrows. Returns whether to swallow it (never: see the .cpp).
    bool key(KWin::KeyboardKeyEvent *event);
    void closed(Window *window);

private:
    void stepSideways(Window *window, Side side);
    void halfView(Window *window);
    void fullView(Window *window);
    Place freeHalf(Window *window) const;
    void selectToward(Qt::Key key);

    ParkedWindows &m_parking;
    FocusRing &m_focusRing;
    // Windows that went to a stash from all of main, to come back as wide.
    std::set<Window *> m_wasFull;
};

} // namespace glance
