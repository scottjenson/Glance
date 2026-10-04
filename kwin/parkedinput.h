// Input to parked windows (docs/dragging-and-parking.md).
//
// Whenever the pointer is over a parked window, its frame is moved
// ("re-anchored") so that the point under the pointer is the same point of
// the window as in the shrunk drawing; the drawing is adjusted so it
// doesn't move. KWin then finds the right spot on its own, at the pointer:
// clicks, hover, the title bar (so it can be dragged out again), popups.
// Where KWin still picks another window (the invisible full-size frame of
// a different window lies above), we point the seat at the window really
// visible under the pointer and forward the events ourselves (see route).
// Known gaps: in the forwarding case the title bar doesn't respond and the
// cursor shape may be wrong.
//
// Icons (ParkedWindows::isIcon: windows in parking, and stashed ones shown
// small): a plain left-button press on one is held back. Dragging it more
// than a few pixels moves the window (KWin's own move, so it grows back
// out of the edge zone); releasing it without dragging passes the press
// and release to the app as a click. So small parked windows can be
// dragged from anywhere and still work as widgets (buttons, scrolling).
// Not for KDE title bars (KWin handles those), clips (dragging a clip
// drags its text) or presses with modifiers.
#pragma once

#include "parked.h"

#include <QMatrix4x4>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <optional>

namespace KWin
{
struct PointerAxisEvent;
struct PointerButtonEvent;
struct PointerMotionEvent;
}

namespace glance
{

class ParkedInput : public QObject
{
    Q_OBJECT

public:
    explicit ParkedInput(ParkedWindows &parking);

    // Pointer events the features left: they reach the window really
    // visible under the pointer. Each returns whether it was taken (then
    // we delivered it ourselves).
    bool motion(KWin::PointerMotionEvent *event);
    bool button(KWin::PointerButtonEvent *event);
    bool axis(KWin::PointerAxisEvent *event);

    // Icons: a plain left press on one is held (returns whether it was);
    // while held, motion decides between click and drag, and the release
    // makes it a click.
    bool holdPress(KWin::PointerButtonEvent *event);
    bool pressPending() const;
    bool pendingMotion(KWin::PointerMotionEvent *event);
    bool releasePending(KWin::PointerButtonEvent *event);

private:
    bool reanchor(Window *window, const QPointF &pos, bool now);
    void anchorLate();
    QMatrix4x4 transformFor(Window *window) const;
    void syncSeatFocus(const QPointF &pos);
    bool route(const QPointF &pos, bool keep, bool now = false);

    ParkedWindows &m_parking;
    // Where we last pointed the seat, and whether KWin disagreed (so we
    // forward events ourselves).
    QPointer<Window> m_target;
    bool m_forwarding = false;
    // When a parked window's frame was last moved under the pointer, and
    // the timer that lines it up where the pointer stopped (see reanchor).
    std::chrono::steady_clock::time_point m_anchoredAt;
    QTimer m_anchorLate;

    // A left-button press on an icon, held back until we know whether it
    // is a click or a drag.
    struct PendingPress
    {
        QPointer<Window> window;
        QPointF position;
        quint32 nativeButton;
        std::chrono::microseconds timestamp;
    };
    std::optional<PendingPress> m_pending;
};

} // namespace glance
