// Keyboard: Meta+arrows move the active window between places,
// Meta+Up/Down give the half and full views, Meta+Alt+arrows select by
// drawn position (docs/keyboard.md).
#pragma once

#include "parked.h"

#include <QObject>

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

private:
    void stepSideways(Window *window, Side side);
    void halfView(Window *window);
    void fullView(Window *window);
    Place freeHalf(Window *window) const;
    void selectToward(Qt::Key key);

    ParkedWindows &m_parking;
    FocusRing &m_focusRing;
};

} // namespace glance
