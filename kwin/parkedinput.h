// Input to parked windows: re-anchoring a parked window's frame under the
// pointer, forwarding events where KWin picks the wrong window, and icon
// presses (click or drag). See docs/dragging-and-parking.md, Input to
// parked windows and Icons.
// Known gaps: when forwarding, the title bar doesn't respond and the
// cursor shape may be wrong.
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
