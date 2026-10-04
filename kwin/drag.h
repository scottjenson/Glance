// Dragging (docs/dragging-and-parking.md, docs/meta-drag.md): while KWin
// moves a window interactively (title bar or Meta+drag), the window is
// drawn shrunk around the cursor once its left or right edge goes into the
// outer quarter of the screen (main stays full size), reaching minScale at
// the screen edge (but no smaller than parkingMinSize). Dropped while
// shrunk, it is parked where it is drawn; dropped in main, it gets its
// original size back.
//
// Meta+drag moves the window like a title-bar drag, with two additions.
// Acceleration: horizontally the window gets ahead of the pointer, more
// the longer you keep moving fast in one direction (gain up to
// leadMaxGain); reversing or slowing down goes back to 1:1, so corrections
// are precise. The screen edges stop it, and overshoot isn't stored. Pause
// to snap: holding still for snapDwell snaps the window to the region it
// is in (see snapTargetAt: a half of main or, in a middle band, all of
// main, both at full height; a stash; parking at the very edge). From then
// on the drag is in snapping mode: moving into another region snaps there
// (no sizes in between); releasing the mouse keeps it, releasing Meta lets
// it follow the pointer again (see leadStep).
#pragma once

#include "parked.h"

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QTimer>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

namespace glance
{

class WindowDrag : public QObject
{
    Q_OBJECT

public:
    explicit WindowDrag(ParkedWindows &parking);
    ~WindowDrag() override;

    // The window being dragged (and drawn scaled), if any.
    Window *window() const;
    // KWin moved `window` interactively (a step), or the move ended.
    void step(Window *window);
    void finished(Window *window);
    // Meta pressed or released: switch between gesture and normal drag.
    void modifiersChanged();
    // Each frame: a glide between the two goes on (moving the window
    // repaints what changed); after painting, the next frame is asked for
    // while it does.
    void prePaintScreen();
    void postPaintScreen();

private:
    using Parked = ParkedWindows::Parked;

    // A Meta+drag snap's target place.
    struct Gesture
    {
        int key; // tells targets apart
        QRectF drawn; // where the window is shown meanwhile
        std::optional<Place> place = std::nullopt;
    };

    QRectF followRect(Window *window, const RectF &frame, const QPointF &cursor);
    qreal holdScale(const RectF &frame, const QPointF &cursor, qreal rule, const RectF &screen);
    std::optional<Gesture> leadStep(Window *window, const QPointF &cursor);
    void updateLead(Window *window, const QPointF &cursor, qreal dx, std::chrono::steady_clock::time_point now);
    std::optional<Gesture> snapTargetAt(Window *window, const QPointF &point) const;
    void commitGesture(Window *window, const Gesture &gesture, const QRectF &from);

    ParkedWindows &m_parking;
    // The window being dragged while we draw it scaled, its original size,
    // and its scale relative to that.
    QPointer<Window> m_dragged;
    QSizeF m_dragOriginal;
    qreal m_dragScale = 1.0;
    // A stashed window keeps its size when a drag starts (see followRect):
    // the held scale, and which side of it the edge rule was on (0: not
    // known yet).
    std::optional<qreal> m_dragHold;
    qreal m_dragHoldSide = 0;
    // Where the dragged window is drawn now, the current gesture target,
    // and the glide between them.
    QRectF m_dragDisplayed;
    std::optional<Gesture> m_dragGesture;
    // Acceleration state of the current Meta+drag (see leadStep): how far
    // the window is ahead of the pointer horizontally, the direction and
    // length of the current run, movement against it so far (jitter until
    // reversalJitter), the last pointer position and recent ones (for the
    // speed); the pause-to-snap target, where the pointer was then, and
    // the timer.
    qreal m_leadX = 0;
    int m_leadDir = 0;
    qreal m_leadRun = 0;
    qreal m_leadAgainst = 0;
    QPointF m_leadLast;
    std::vector<std::pair<std::chrono::steady_clock::time_point, QPointF>> m_leadSamples;
    std::optional<Gesture> m_snapped;
    QTimer m_snapDwell;
    // Snapping mode: the window's center and the pointer (plus lead) at the
    // first snap; the region follows the pointer's movement from there.
    QPointF m_snapAnchor;
    QPointF m_snapPointer;
    int m_dragModeKey = -1;
    bool m_dragAnimating = false;
    QRectF m_dragAnimFrom;
    std::chrono::steady_clock::time_point m_dragAnimStart;
};

} // namespace glance
