// Keyboard (see keyboard.h).
#include "keyboard.h"

#include "focusring.h"
#include "geometry.h"

#include <core/output.h>
#include <input.h>
#include <input_event.h>
#include <pointer_input.h>
#include <window.h>
#include <workspace.h>

#include <algorithm>
#include <cmath>

using namespace KWin;

namespace glance
{

Keyboard::Keyboard(ParkedWindows &parking, FocusRing &focusRing)
    : m_parking(parking)
    , m_focusRing(focusRing)
{
}

bool Keyboard::key(KeyboardKeyEvent *event)
{
    const Qt::Key key = event->key;
    if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down) {
        return false;
    }
    if (event->modifiers == (Qt::MetaModifier | Qt::AltModifier)) {
        if (event->state != KeyboardKeyState::Released && !workspace()->moveResizeWindow()) {
            selectToward(key);
        }
        return false; // passed on, like Meta+arrows below
    }
    if (event->modifiers != Qt::MetaModifier) {
        return false;
    }
    Window *window = workspace()->activeWindow();
    if (!window || !window->isNormalWindow() || window->isFullScreen() || !window->isMovable()
        || !window->isResizable() || workspace()->moveResizeWindow() || !window->windowItem()) {
        return false;
    }
    if (event->state == KeyboardKeyState::Pressed) {
        switch (key) {
        case Qt::Key_Left:
            stepSideways(window, Side::Left);
            break;
        case Qt::Key_Right:
            stepSideways(window, Side::Right);
            break;
        case Qt::Key_Up:
            halfView(window);
            break;
        default:
            fullView(window);
            break;
        }
    }
    // Pass the key on: KDE's shortcut system must see it, or it takes
    // releasing Meta as Meta tapped alone and opens the launcher. Its own
    // quick tiling on these keys is disabled (see KdeIntegration).
    return false;
}

void Keyboard::closed(Window *window)
{
    m_wasFull.erase(window);
}

// One step towards `side` along: parking L, stash L, half L, half R,
// stash R, parking R. A free window goes to the half on that side. A
// window in all of main goes straight to the stash on that side, and
// from there back into all of main (m_wasFull).
void Keyboard::stepSideways(Window *window, Side side)
{
    const bool left = side == Side::Left;
    const Place from = m_parking.placeOf(window);
    Place to;
    if (from == Place::Free) {
        to = left ? Place::HalfLeft : Place::HalfRight;
    } else if (from == Place::Full) {
        to = left ? Place::StashLeft : Place::StashRight;
        m_wasFull.insert(window);
    } else {
        const int i = placeIndex(from);
        const int j = std::clamp(i + (left ? -1 : 1), 0, 5);
        if (i == j) {
            return;
        }
        to = placeOrder[j];
        const bool backIntoMain = (from == Place::StashLeft && to == Place::HalfLeft)
            || (from == Place::StashRight && to == Place::HalfRight);
        if (backIntoMain && m_wasFull.erase(window)) {
            to = Place::Full;
        } else if (from == Place::HalfLeft || from == Place::HalfRight) {
            m_wasFull.erase(window);
        }
    }
    m_parking.moveTo(window, to);
}

// Meta+Up, the half view: a half of main at full height. A window in a
// half stays in it; one in all of main goes to a free half (see
// freeHalf); any other to the half nearer to it. Not for parked windows.
void Keyboard::halfView(Window *window)
{
    if (m_parking.isParkedNotRestoring(window)) {
        return;
    }
    const Place place = m_parking.placeOf(window);
    Place half = place;
    if (place == Place::Full) {
        half = freeHalf(window);
    } else if (place != Place::HalfLeft && place != Place::HalfRight) {
        const RectF screen = window->output()->geometryF();
        half = m_parking.currentlyDrawn(window).center().x() < screen.x() + screen.width() / 2 ? Place::HalfLeft : Place::HalfRight;
    }
    m_parking.fillHalf(window, half);
}

// Meta+Down, the full view: all of main at full height. Not for parked
// windows.
void Keyboard::fullView(Window *window)
{
    if (m_parking.isParkedNotRestoring(window)) {
        return;
    }
    releaseKdeState(window);
    const RectF screen = window->output()->geometryF();
    const RectF area = workspace()->clientArea(MaximizeArea, window);
    const qreal zoneWidth = screen.width() * zoneFraction;
    m_parking.resizeAnimated(window, RectF(screen.x() + zoneWidth, area.y(), 2 * zoneWidth, area.height()), m_parking.currentlyDrawn(window));
}

// The half of main for `window`: the free one if the other is taken by
// another window, else (both free or both taken) the left one.
Place Keyboard::freeHalf(Window *window) const
{
    bool taken[2] = {false, false};
    for (Window *other : workspace()->stackingOrder()) {
        if (other == window || !manageable(other) || other->output() != window->output() || m_parking.isParkedNotRestoring(other)) {
            continue;
        }
        const Place place = m_parking.placeOf(other);
        if (place == Place::HalfLeft) {
            taken[0] = true;
        } else if (place == Place::HalfRight) {
            taken[1] = true;
        }
    }
    return taken[0] && !taken[1] ? Place::HalfRight : Place::HalfLeft;
}

// Meta+Alt+arrow: activate the nearest window in that direction, by
// where windows are drawn (centers), scored like KWin's own
// Workspace::switchWindow: distance along the arrow, plus how far off
// to the side, plus a penalty for being far off to the side but close.
void Keyboard::selectToward(Qt::Key key)
{
    Window *active = workspace()->activeWindow();
    const QPointF from = active ? m_parking.currentlyDrawn(active).center() : input()->pointer()->pos();
    Window *best = nullptr;
    qreal bestScore = 0;
    for (Window *window : workspace()->stackingOrder()) {
        if (window == active || !m_parking.switchable(window)) {
            continue;
        }
        const QPointF to = m_parking.currentlyDrawn(window).center();
        qreal distance;
        qreal offset;
        switch (key) {
        case Qt::Key_Left:
            distance = from.x() - to.x();
            offset = std::abs(to.y() - from.y());
            break;
        case Qt::Key_Right:
            distance = to.x() - from.x();
            offset = std::abs(to.y() - from.y());
            break;
        case Qt::Key_Up:
            distance = from.y() - to.y();
            offset = std::abs(to.x() - from.x());
            break;
        default:
            distance = to.y() - from.y();
            offset = std::abs(to.x() - from.x());
            break;
        }
        if (distance <= 0) {
            continue;
        }
        const qreal score = distance + offset + offset * offset / distance;
        if (!best || score < bestScore) {
            best = window;
            bestScore = score;
        }
    }
    if (best) {
        workspace()->activateWindow(best);
        m_focusRing.bounce(best);
    }
}

} // namespace glance
