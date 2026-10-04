// Input to parked windows (see parkedinput.h).
#include "parkedinput.h"

#include <core/output.h>
#include <input.h>
#include <input_event.h>
#include <options.h>
#include <pointer_input.h>
#include <wayland/seat.h>
#include <wayland/surface.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <algorithm>
#include <cmath>

using namespace KWin;

namespace glance
{

ParkedInput::ParkedInput(ParkedWindows &parking)
    : m_parking(parking)
{
    m_anchorLate.setSingleShot(true);
    connect(&m_anchorLate, &QTimer::timeout, this, &ParkedInput::anchorLate);
}

bool ParkedInput::motion(PointerMotionEvent *event)
{
    if (!route(event->position, event->buttons != Qt::NoButton)) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event->timestamp);
    seat->notifyPointerMotion(event->position);
    return true;
}

bool ParkedInput::button(PointerButtonEvent *event)
{
    const bool pressed = event->state == PointerButtonState::Pressed;
    // Keep the target of a press until its release.
    if (!route(event->position, !pressed || event->buttons != event->button, true)) {
        return false;
    }
    if (pressed && m_target) {
        workspace()->activateWindow(m_target);
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event->timestamp);
    seat->notifyPointerButton(event->nativeButton, event->state);
    return true;
}

bool ParkedInput::axis(PointerAxisEvent *event)
{
    if (!route(event->position, event->buttons != Qt::NoButton)) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event->timestamp);
    seat->notifyPointerAxis(event->orientation, event->delta, event->deltaV120, event->source, event->inverted);
    return true;
}

bool ParkedInput::pressPending() const
{
    return m_pending.has_value();
}

// --- Icons: click or drag ---

// A plain left press on an icon-like parked window (not on a KDE title
// bar): hold it back. Returns whether it was held.
bool ParkedInput::holdPress(PointerButtonEvent *event)
{
    if (event->button != Qt::LeftButton || event->buttons != Qt::LeftButton
        || event->modifiers != Qt::NoModifier || workspace()->moveResizeWindow()) {
        return false;
    }
    Window *window = m_parking.pick(event->position);
    // Clips get their presses: dragging a clip drags its text, and an
    // app can only start a drag from a press it received.
    if (!window || isClip(window) || !m_parking.isIcon(window)) {
        return false;
    }
    // Line the frame up with the pointer, so KWin sees what's under it.
    route(event->position, false, true);
    if (input()->pointer()->decoration()) {
        return false;
    }
    m_pending = PendingPress{window, event->position, event->nativeButton, event->timestamp};
    return true;
}

// While a press is held: far enough away, it becomes a move of the
// window. Until then the app doesn't see the pointer move.
bool ParkedInput::pendingMotion(PointerMotionEvent *event)
{
    const QPointF delta = event->position - m_pending->position;
    if (std::hypot(delta.x(), delta.y()) < dragThreshold) {
        return true;
    }
    const PendingPress press = *m_pending;
    m_pending.reset();
    if (press.window) {
        // The frame was anchored at the press position, so KWin's move
        // keeps the grabbed spot under the cursor. KWin then handles
        // this and the following motion and the release.
        press.window->performMousePressCommand(Options::MouseMove, press.position);
    }
    return false;
}

// Released without dragging: it was a click. Give the app the press and
// this release.
bool ParkedInput::releasePending(PointerButtonEvent *event)
{
    const PendingPress press = *m_pending;
    m_pending.reset();
    if (!press.window) {
        return true;
    }
    workspace()->activateWindow(press.window);
    route(event->position, false, true); // points the seat at the window
    auto seat = waylandServer()->seat();
    seat->setTimestamp(press.timestamp);
    seat->notifyPointerButton(press.nativeButton, PointerButtonState::Pressed);
    seat->notifyPointerFrame();
    seat->setTimestamp(event->timestamp);
    seat->notifyPointerButton(event->nativeButton, PointerButtonState::Released);
    seat->notifyPointerFrame();
    return true;
}

// --- Re-anchoring and forwarding ---

// Move a parked window's frame so that the point of the window drawn at
// `pos` is also at `pos` in the frame. The drawing stays in place.
// Returns whether they line up; if not, the caller forwards events with
// transformFor, which is exact without moving anything.
// Each move is a real geometry change in KWin (window rules, the
// window's monitor, the app is told), so pointer motion moves the frame
// at most once per refresh; `now` (buttons) moves it at once. When
// motion skips a move, anchorLate lines the frame up a refresh later, so
// where the pointer stops (tooltips, menus) the frame is right.
// The frame swings far past the drawing: never so far that its centre
// leaves the window's monitor, or KWin would give it to the next one.
bool ParkedInput::reanchor(Window *window, const QPointF &pos, bool now)
{
    if (workspace()->moveResizeWindow() == window) {
        return true;
    }
    const QPointF drawn = m_parking.displayRect(m_parking.at(window)).topLeft();
    const QPointF topLeft = pos - (pos - drawn) / m_parking.scaleOf(window);
    const RectF frame = window->frameGeometry();
    if (topLeft == frame.topLeft()) {
        return true;
    }
    if (!window->output()->geometryF().contains(frame.translated(topLeft - frame.topLeft()).center())) {
        return false;
    }
    const auto clock = std::chrono::steady_clock::now();
    const auto period = std::chrono::microseconds(1000000000 / std::max<uint32_t>(window->output()->refreshRate(), 1000));
    if (!now && clock < m_anchoredAt + period) {
        if (!m_anchorLate.isActive()) {
            m_anchorLate.start(std::chrono::ceil<std::chrono::milliseconds>(m_anchoredAt + period - clock));
        }
        return false;
    }
    m_anchorLate.stop();
    m_anchoredAt = clock;
    window->move(topLeft);
    return true;
}

// Motion skipped a move (see reanchor): line the frame up where the
// pointer is now. Moving it makes KWin reset the seat's mapping for its
// own pointer focus; when we point the seat ourselves, set ours again.
void ParkedInput::anchorLate()
{
    if (!m_target || !m_parking.isParked(m_target) || m_pending || workspace()->moveResizeWindow()
        || waylandServer()->seat()->isDragPointer()) {
        return;
    }
    const QPointF pos = input()->pointer()->pos();
    if (!m_parking.drawnRect(m_target).contains(pos) || !reanchor(m_target, pos, true)) {
        return;
    }
    if (m_forwarding) {
        waylandServer()->seat()->setFocusedPointerSurfaceTransformation(transformFor(m_target));
    }
}

// Maps a global position to the window's surface-local position.
QMatrix4x4 ParkedInput::transformFor(Window *window) const
{
    auto *it = m_parking.find(window);
    if (!it) {
        return window->inputTransformation();
    }

    const QPointF drawn = m_parking.displayRect(*it).topLeft();
    const qreal scale = m_parking.scaleOf(window);
    const QPointF frame = window->frameGeometry().topLeft();
    const QPointF buffer = window->bufferGeometry().topLeft();
    QMatrix4x4 m;
    m.translate(frame.x() - buffer.x(), frame.y() - buffer.y());
    m.scale(1.0 / scale, 1.0 / scale);
    m.translate(-drawn.x(), -drawn.y());
    return m;
}

// When we forward events ourselves, we point the seat at another surface
// than KWin's focus window, and KWin doesn't notice: it only re-points
// the seat when its own focus changes. Before leaving events to KWin
// again, point the seat back at KWin's focus window, or KWin would
// deliver them to whatever surface we last chose (e.g. the desktop).
void ParkedInput::syncSeatFocus(const QPointF &pos)
{
    auto seat = waylandServer()->seat();
    Window *focus = input()->pointer()->focus();
    SurfaceInterface *surface = focus ? focus->surface() : nullptr;
    if (seat->focusedPointerSurface() == surface) {
        return;
    }
    if (surface) {
        seat->notifyPointerEnter(surface, pos, focus->inputTransformation());
    } else {
        seat->notifyPointerLeave();
    }
}

// Make the pointer event at `pos` reach the window really visible there.
// Returns whether we must forward it ourselves because KWin's own pick is
// wrong, or the frame isn't lined up (see reanchor). With `keep`, a
// button is held: stay with the current target. `now`: a button event,
// line the frame up at once.
bool ParkedInput::route(const QPointF &pos, bool keep, bool now)
{
    // KWin's own moves, drag and drop (and no parked windows) need
    // nothing from us. During drag and drop, re-anchoring would move the
    // invisible full-size frame of the window the drag started in (a
    // clip) under the pointer, where it would catch the drop.
    if (m_parking.empty() || workspace()->moveResizeWindow() || waylandServer()->seat()->isDragPointer()) {
        m_target = nullptr;
        m_forwarding = false;
        return false;
    }

    auto pointer = input()->pointer();
    if (keep) {
        bool exact = true;
        if (m_target && m_parking.isParked(m_target)) {
            exact = reanchor(m_target, pos, now);
            if (m_forwarding || !exact) {
                waylandServer()->seat()->setFocusedPointerSurfaceTransformation(transformFor(m_target));
            }
        }
        return m_forwarding || !exact;
    }

    Window *target = m_parking.pick(pos);
    const bool involved = (target && m_parking.isParked(target)) || (pointer->hover() && m_parking.isParked(pointer->hover()));
    if (!involved) {
        // Nothing parked here: KWin knows best.
        m_target = nullptr;
        m_forwarding = false;
        syncSeatFocus(pos);
        return false;
    }

    bool exact = true;
    if (target && m_parking.isParked(target)) {
        exact = reanchor(target, pos, now);
        // KWin picked its window before the frame moved; pick again.
        pointer->update();
    }

    m_target = target;
    m_forwarding = target != pointer->hover() || !exact;
    if (!m_forwarding) {
        syncSeatFocus(pos);
        return false;
    }

    auto seat = waylandServer()->seat();
    SurfaceInterface *surface = target ? target->surface() : nullptr;
    if (seat->focusedPointerSurface() != surface) {
        if (surface) {
            seat->notifyPointerEnter(surface, pos, transformFor(target));
        } else {
            seat->notifyPointerLeave();
        }
    } else if (surface) {
        seat->setFocusedPointerSurfaceTransformation(transformFor(target));
    }
    return true;
}

} // namespace glance
