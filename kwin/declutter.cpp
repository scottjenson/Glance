// Declutter (see declutter.h).
#include "declutter.h"

#include <core/output.h>
#include <input_event.h>
#include <window.h>
#include <workspace.h>

#include <QGuiApplication>
#include <QStyleHints>
#include <QTransform>

#include <algorithm>
#include <cmath>

using namespace KWin;

namespace glance
{

Declutter::Declutter(ParkedWindows &parking)
    : m_parking(parking)
{
}

// Meta+double-click (left button). The first click is left to KWin (a
// Meta+press starts a move, which a release without motion ends); the
// second press and its release are taken. Returns whether the event was.
bool Declutter::button(PointerButtonEvent *event)
{
    if (event->state == PointerButtonState::Released) {
        if (m_swallowRelease && event->button == Qt::LeftButton) {
            m_swallowRelease = false;
            return true;
        }
        return false;
    }
    if (event->button != Qt::LeftButton || event->modifiers != Qt::MetaModifier
        || event->buttons != Qt::LeftButton) {
        m_metaPress.reset();
        return false;
    }
    const std::chrono::milliseconds interval{QGuiApplication::styleHints()->mouseDoubleClickInterval()};
    if (m_metaPress && event->timestamp - m_metaPress->second <= interval
        && std::hypot(event->position.x() - m_metaPress->first.x(), event->position.y() - m_metaPress->first.y()) <= dragThreshold
        && !workspace()->moveResizeWindow()) {
        m_metaPress.reset();
        Window *under = m_parking.pick(event->position);
        if (under && !under->isDesktop() && !manageable(under)) {
            return false; // a panel or the like: not ours
        }
        m_swallowRelease = true;
        toggle(under && !under->isDesktop() ? under : nullptr, event->position);
        return true;
    }
    m_metaPress = std::make_pair(event->position, event->timestamp);
    return false;
}

// `target` (or, if null, the desktop at `pos`) was Meta+double-clicked:
// undo the last declutter if it was for the same target and the target
// is still where it put it; else declutter.
void Declutter::toggle(Window *target, const QPointF &pos)
{
    const bool again = m_last
        && (target ? m_last->target == target && m_parking.placeOf(target) == m_last->half : m_last->desktop);
    if (again) {
        undo();
        return;
    }
    LogicalOutput *output = target ? target->output() : workspace()->outputAt(pos);
    if (!output) {
        return;
    }
    const RectF screen = output->geometryF();
    const qreal middle = screen.x() + screen.width() / 2;

    Layout saved{.desktop = !target, .target = target, .half = Place::Free, .saved = {}};
    std::vector<Window *> movers; // from main to a stash
    std::vector<Window *> stashed[2]; // already in the left / right stash
    for (Window *window : workspace()->stackingOrder()) {
        if (!manageable(window) || window->output() != output) {
            continue;
        }
        auto *it = m_parking.find(window);
        const bool parked = it && !it->restoring;
        saved.saved.push_back(Saved{.window = window,
                                    .parked = parked ? std::optional<Parked>(*it) : std::nullopt,
                                    .frame = window->moveResizeGeometry(),
                                    .maximize = window->maximizeMode()});
        if (window == target) {
            continue;
        }
        if (!parked) {
            movers.push_back(window);
        } else if (const int area = m_parking.areaOf(window); area == 1 || area == 3) {
            stashed[area == 1 ? 0 : 1].push_back(window);
        }
    }

    // Balance the stashes: the leftmost `toLeft` movers go left, the rest
    // right, so both end up with about as many windows.
    std::sort(movers.begin(), movers.end(), [this](Window *a, Window *b) {
        return m_parking.currentlyDrawn(a).center().x() < m_parking.currentlyDrawn(b).center().x();
    });
    const int count = int(movers.size());
    const int toLeft = std::clamp(int(std::lround((count + int(stashed[1].size()) - int(stashed[0].size())) / 2.0)), 0, count);
    stashed[0].insert(stashed[0].end(), movers.begin(), movers.begin() + toLeft);
    stashed[1].insert(stashed[1].end(), movers.begin() + toLeft, movers.end());

    if (target) {
        saved.half = m_parking.currentlyDrawn(target).center().x() < middle ? Place::HalfLeft : Place::HalfRight;
        m_parking.fillHalf(target, saved.half);
    }
    // Each stash at one scale, so its column lines up.
    for (int side = 0; side < 2; ++side) {
        if (movers.empty() || stashed[side].empty()) {
            continue;
        }
        const Place stash = side == 0 ? Place::StashLeft : Place::StashRight;
        const qreal scale = m_parking.fittingScale(output, stashed[side]);
        for (Window *window : stashed[side]) {
            m_parking.moveTo(window, stash, scale);
        }
        m_parking.arrangeArea(side == 0 ? 1 : 3, output, nullptr);
    }
    if (target) {
        workspace()->activateWindow(target);
    }
    m_last = std::move(saved);
}

// Everything back as it was before the last declutter (windows closed
// since are skipped).
void Declutter::undo()
{
    const Layout declutter = std::move(*m_last);
    m_last.reset();
    for (const Saved &saved : declutter.saved) {
        Window *window = saved.window;
        if (!window || window->isDeleted() || !window->windowItem()) {
            continue;
        }
        const QRectF from = m_parking.currentlyDrawn(window);
        if (saved.parked) {
            m_parking.park(window, saved.parked->shown, saved.parked->original);
            m_parking.animate(window, from);
        } else if (saved.maximize != MaximizeRestore) {
            m_parking.erase(window);
            m_parking.setDrawTransform(window, QTransform());
            window->maximize(saved.maximize);
        } else if (m_parking.isParked(window) || window->moveResizeGeometry() != saved.frame) {
            m_parking.resizeAnimated(window, saved.frame, from);
        }
    }
    if (declutter.target) {
        workspace()->activateWindow(declutter.target);
    }
}

} // namespace glance
