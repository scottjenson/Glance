// Dragging (see drag.h).
#include "drag.h"

#include "geometry.h"

#include <core/output.h>
#include <input.h>
#include <pointer_input.h>
#include <scene/windowitem.h>
#include <window.h>
#include <workspace.h>

#include <QTransform>

#include <algorithm>
#include <cmath>

using namespace KWin;

namespace glance
{

WindowDrag::WindowDrag(ParkedWindows &parking)
    : m_parking(parking)
{
    m_snapDwell.setSingleShot(true);
    m_snapDwell.setInterval(snapDwell);
    connect(&m_snapDwell, &QTimer::timeout, this, [this]() {
        if (m_dragged && !m_snapped && (input()->keyboardModifiers() & Qt::MetaModifier)) {
            m_snapAnchor = m_dragDisplayed.center();
            m_snapPointer = input()->pointer()->pos() + QPointF(m_leadX, 0);
            m_snapped = snapTargetAt(m_dragged, m_snapAnchor);
            m_leadRun = 0;
            step(m_dragged);
        }
    });
}

WindowDrag::~WindowDrag()
{
    if (m_dragged && m_dragged->windowItem()) {
        m_dragged->windowItem()->setTransform(QTransform());
    }
}

Window *WindowDrag::window() const
{
    return m_dragged;
}

void WindowDrag::modifiersChanged()
{
    // Once KWin has updated its modifier state.
    QTimer::singleShot(0, this, [this]() {
        if (m_dragged) {
            step(m_dragged);
        }
    });
}

void WindowDrag::prePaintScreen()
{
    if (m_dragged && m_dragAnimating) {
        step(m_dragged);
    }
}

bool WindowDrag::animating() const
{
    return m_dragAnimating;
}

// KWin has moved the dragged window so the grabbed spot is under the
// cursor. Draw it scaled around the cursor by the edge rule, or, while
// Meta is held and the drag matches a gesture, at the gesture's target;
// switching between the two glides. A parked window being dragged stops
// being parked: its frame was re-anchored around the cursor when it was
// grabbed, so the drag continues from where it is drawn.
void WindowDrag::step(Window *window)
{
    if (!window->isInteractiveMove() || !window->windowItem()) {
        return;
    }
    const RectF frame = window->frameGeometry();
    const QPointF cursor = input()->pointer()->pos();
    if (m_dragged != window) {
        // Start of a drag. The scale is relative to the original size,
        // which a parked (resized) window remembers.
        auto *it = m_parking.find(window);
        const bool parked = it && !it->restoring;
        m_dragOriginal = it ? it->original : QSizeF(frame.width(), frame.height());
        m_dragDisplayed = m_parking.currentlyDrawn(window);
        m_dragHold.reset();
        m_dragHoldSide = 0;
        if (parked && !m_parking.isParkingArea(m_parking.areaOf(window))) {
            m_dragHold = it->shown.width() / it->original.width();
        }
        m_dragGesture.reset();
        m_dragModeKey = -1;
        m_leadX = 0;
        m_leadDir = 0;
        m_leadRun = 0;
        m_leadAgainst = 0;
        m_leadLast = cursor;
        m_leadSamples.clear();
        m_snapped.reset();
        m_snapDwell.stop();
        m_dragAnimating = false;
        const auto closeRanks = m_parking.leaving(window);
        m_parking.erase(window);
        m_dragged = window;
        closeRanks();
    }

    const std::optional<Gesture> gesture = leadStep(window, cursor);
    // The edge rule around where the window is: the pointer plus the
    // window's lead (the frame shifted with it keeps the grab offset).
    const QRectF want = gesture ? gesture->drawn
                                : followRect(window, RectF(frame.x() + m_leadX, frame.y(), frame.width(), frame.height()),
                                             cursor + QPointF(m_leadX, 0));
    const int key = gesture ? gesture->key : -1;
    if (key != m_dragModeKey) {
        m_dragModeKey = key;
        m_dragAnimFrom = m_dragDisplayed;
        m_dragAnimStart = std::chrono::steady_clock::now();
        m_dragAnimating = true;
    }
    QRectF shown = want;
    if (m_dragAnimating) {
        const auto elapsed = std::chrono::steady_clock::now() - m_dragAnimStart;
        const qreal t = std::clamp(std::chrono::duration<qreal>(elapsed) / glance::animationTime, 0.0, 1.0);
        if (t >= 1.0) {
            m_dragAnimating = false;
        } else {
            shown = lerpRect(m_dragAnimFrom, want, 1.0 - std::pow(1.0 - t, 3));
        }
    }
    m_dragDisplayed = shown;
    m_dragGesture = gesture;

    // The item's coordinates start at the frame's top-left corner.
    QTransform transform;
    transform.translate(shown.x() - frame.x(), shown.y() - frame.y());
    transform.scale(shown.width() / frame.width(), shown.height() / frame.height());
    m_parking.setDrawTransform(window, transform);
}

// Where the edge rule draws the dragged window: scaled around the cursor.
QRectF WindowDrag::followRect(Window *window, const RectF &frame, const QPointF &cursor)
{
    const RectF screen = window->moveResizeOutput()->geometryF();
    const qreal left = frame.x();
    const qreal right = frame.x() + frame.width();
    // From the current frame size to the original size.
    const qreal grow = m_dragOriginal.width() / frame.width();

    // Full size while the window stays within main (the middle half);
    // shrinks as its left or right edge goes into the edge zone.
    const qreal zoneWidth = screen.width() * zoneFraction;
    qreal total = std::min({1.0,
                            edgeScale(cursor.x() - screen.x(), (cursor.x() - left) * grow, zoneWidth),
                            edgeScale(screen.x() + screen.width() - cursor.x(), (right - cursor.x()) * grow, zoneWidth)});
    total = std::max(total, parkingScale(m_dragOriginal));
    if (m_dragHold) {
        total = holdScale(frame, cursor, total, screen);
    }
    m_dragScale = total;
    // The scale to draw the current frame at.
    const qreal scale = total * grow;

    // Fallback when even minScale doesn't fit: slide it back on screen
    // (the cursor then detaches from the grabbed spot).
    const qreal drawnLeft = cursor.x() + (left - cursor.x()) * scale;
    const qreal shift = shiftOntoScreen(drawnLeft, frame.width() * scale, screen);
    return QRectF(drawnLeft + shift, cursor.y() + (frame.y() - cursor.y()) * scale,
                  frame.width() * scale, frame.height() * scale);
}

// A window dragged out of a stash keeps the size it had there (set by
// the drop or by Meta+wheel), so the drag doesn't start with a jump:
// the edge rule (`rule`) takes over once it reaches that size (no jump
// then), or with a glide once the window's center leaves the stash.
qreal WindowDrag::holdScale(const RectF &frame, const QPointF &cursor, qreal rule, const RectF &screen)
{
    const qreal hold = *m_dragHold;
    const qreal side = rule - hold;
    if (m_dragHoldSide == 0) {
        m_dragHoldSide = side < 0 ? -1 : 1;
    }
    if (side * m_dragHoldSide <= 0) {
        m_dragHold.reset();
        return rule;
    }
    const qreal scale = hold * m_dragOriginal.width() / frame.width();
    const qreal centerX = cursor.x() + (frame.x() + frame.width() / 2 - cursor.x()) * scale - screen.x();
    const qreal zoneWidth = screen.width() * zoneFraction;
    const qreal band = zoneWidth * parkingBand;
    const bool inStash = (centerX >= band && centerX < zoneWidth)
        || (centerX <= screen.width() - band && centerX > screen.width() - zoneWidth);
    if (!inStash) {
        m_dragHold.reset();
        m_dragAnimFrom = m_dragDisplayed;
        m_dragAnimStart = std::chrono::steady_clock::now();
        m_dragAnimating = true;
        return rule;
    }
    return hold;
}

// Acceleration and snapping, on every drag step: updates the window's
// lead (see updateLead) and returns the snap target, if snapping. The
// first snap comes from m_snapDwell (a pause); then, in snapping mode,
// the target follows the region the window would be in (the pointer's
// movement since the snap, added to the window's center then). Without
// Meta the gain is 1 (the lead stays) and there is no snapping.
std::optional<WindowDrag::Gesture> WindowDrag::leadStep(Window *window, const QPointF &cursor)
{
    const auto now = std::chrono::steady_clock::now();
    const bool moved = cursor != m_leadLast;
    const qreal dx = cursor.x() - m_leadLast.x();
    m_leadLast = cursor;
    if (moved) {
        m_leadSamples.emplace_back(now, cursor);
        std::erase_if(m_leadSamples, [&](const auto &sample) {
            return now - sample.first > leadSampleTime;
        });
    }
    if (!(input()->keyboardModifiers() & Qt::MetaModifier)) {
        m_snapDwell.stop();
        m_leadRun = 0;
        m_snapped.reset();
        return std::nullopt;
    }
    if (moved) {
        updateLead(window, cursor, dx, now);
        if (!m_snapped) {
            m_snapDwell.start();
        }
    }
    if (m_snapped) {
        const QPointF point = m_snapAnchor + (cursor + QPointF(m_leadX, 0) - m_snapPointer);
        if (auto target = snapTargetAt(window, point); target->key != m_snapped->key) {
            m_snapped = target;
        }
    }
    return m_snapped;
}

// Acceleration: each horizontal pointer movement moves the window that
// much times the gain, which grows from 1 to leadMaxGain over the run
// (movement in one direction); a reversal (reversalJitter the other way)
// or moving slower than leadSlow starts a new run at 1. The lead keeps
// the window between the screen edges, so overshoot isn't stored.
void WindowDrag::updateLead(Window *window, const QPointF &cursor, qreal dx, std::chrono::steady_clock::time_point now)
{
    // Slow: 1:1 (precise).
    const auto &[t0, p0] = m_leadSamples.front();
    const qreal dt = std::chrono::duration<qreal>(now - t0).count();
    if (dt <= 0 || std::abs(cursor.x() - p0.x()) / dt < leadSlow) {
        m_leadRun = 0;
        m_leadAgainst = 0;
        return;
    }
    if (dx == 0) {
        return;
    }
    const RectF screen = window->moveResizeOutput()->geometryF();
    const int dir = dx < 0 ? -1 : 1;
    qreal gain = 1.0;
    if (m_leadDir == 0 || dir == m_leadDir) {
        m_leadDir = dir;
        m_leadRun += std::abs(dx);
        m_leadAgainst = 0;
        gain = 1.0 + (leadMaxGain - 1.0) * std::min(1.0, m_leadRun / (screen.width() * leadBuild));
    } else {
        m_leadAgainst += std::abs(dx);
        if (m_leadAgainst >= reversalJitter) {
            m_leadDir = dir; // reversed: a new run, at 1:1
            m_leadRun = m_leadAgainst;
            m_leadAgainst = 0;
        }
    }
    m_leadX += (gain - 1.0) * dx;
    const qreal x = std::clamp(cursor.x() + m_leadX, screen.x(), screen.x() + screen.width());
    m_leadX = x - cursor.x();
}

// The snap target for a window centered at `point`, by region: in the
// parking band parking, elsewhere in an edge zone the stash, in main a
// half, or all of main in the middle band (snapFullBand); halves and all
// of main at full height.
std::optional<WindowDrag::Gesture> WindowDrag::snapTargetAt(Window *window, const QPointF &point) const
{
    const RectF screen = window->moveResizeOutput()->geometryF();
    const RectF area = workspace()->clientArea(MaximizeArea, window);
    const qreal zoneWidth = screen.width() * zoneFraction;
    const qreal x = point.x() - screen.x();
    const qreal width = screen.width();
    Place place;
    if (x < zoneWidth * parkingBand) {
        place = Place::ParkingLeft;
    } else if (x < zoneWidth) {
        place = Place::StashLeft;
    } else if (x > width - zoneWidth * parkingBand) {
        place = Place::ParkingRight;
    } else if (x > width - zoneWidth) {
        place = Place::StashRight;
    } else if (x < width / 2 - width * snapFullBand / 2) {
        place = Place::HalfLeft;
    } else if (x > width / 2 + width * snapFullBand / 2) {
        place = Place::HalfRight;
    } else {
        place = Place::Full;
    }
    QRectF drawn;
    switch (place) {
    case Place::HalfLeft:
    case Place::HalfRight:
        drawn = QRectF(screen.x() + (place == Place::HalfLeft ? zoneWidth : 2 * zoneWidth), area.y(), zoneWidth, area.height());
        break;
    case Place::Full:
        drawn = QRectF(screen.x() + zoneWidth, area.y(), 2 * zoneWidth, area.height());
        break;
    default:
        drawn = m_parking.placeRect(window, place, m_dragOriginal, point.y());
        break;
    }
    return Gesture{.key = int(place), .drawn = drawn, .place = place};
}

// Released with a gesture target: go there, gliding from where it is
// shown.
void WindowDrag::commitGesture(Window *window, const Gesture &gesture, const QRectF &from)
{
    if (!gesture.place) {
        return;
    }
    switch (*gesture.place) {
    case Place::HalfLeft:
    case Place::HalfRight:
    case Place::Full: {
        // At full height: exactly the target rectangle.
        const QRectF &r = gesture.drawn;
        releaseKdeState(window);
        m_parking.resizeAnimated(window, RectF(r.x(), r.y(), r.width(), r.height()), from);
        break;
    }
    default:
        m_parking.commitPlace(window, *gesture.place, m_dragOriginal, gesture.drawn.center().y(), from);
        break;
    }
}

// Dropped: park it where it is drawn, or restore full size.
void WindowDrag::finished(Window *window)
{
    if (window != m_dragged) {
        return;
    }
    m_dragged = nullptr;
    m_dragAnimating = false;
    m_snapDwell.stop();
    m_snapped.reset();
    const std::optional<Gesture> gesture = m_dragGesture;
    m_dragGesture.reset();
    if (!window->windowItem()) {
        return;
    }
    if (gesture) {
        commitGesture(window, *gesture, m_dragDisplayed);
        return;
    }

    const RectF frame = window->frameGeometry();
    const QRectF drawn = window->windowItem()->transform()
                             .mapRect(QRectF(0, 0, frame.width(), frame.height()))
                             .translated(frame.topLeft());

    if (m_dragScale < parkBelow) {
        m_parking.park(window, drawn, m_dragOriginal);
        m_parking.arrange(window);
    } else if (m_dragOriginal != QSizeF(frame.width(), frame.height())) {
        // Back to the original size, keeping the grabbed spot under the
        // cursor (plus the window's lead): the drawing grows around it
        // until the app has resized.
        const QPointF cursor = input()->pointer()->pos();
        const QPointF lead(m_leadX, 0);
        const qreal grow = m_dragOriginal.width() / frame.width();
        const QRectF target(cursor + lead - (cursor - frame.topLeft()) * grow, m_dragOriginal);
        m_parking.set(window, Parked{.shown = target, .original = m_dragOriginal, .restoring = true});
        qInfo("glance: %s: restore to %.0fx%.0f", qPrintable(window->caption()),
              m_dragOriginal.width(), m_dragOriginal.height());
        window->moveResize(RectF(target.topLeft(), m_dragOriginal));
        m_parking.applyParked(window);
    } else {
        // Full size: where it is drawn (ahead of the pointer by the lead).
        if (m_leadX != 0) {
            window->move(frame.topLeft() + QPointF(m_leadX, 0));
        }
        m_parking.setDrawTransform(window, QTransform());
    }
}

} // namespace glance
