// Meta+wheel (see wheel.h).
#include "wheel.h"

#include "geometry.h"
#include "previews.h"

#include <core/output.h>
#include <input_event.h>
#include <wayland/seat.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <algorithm>
#include <cmath>

using namespace KWin;

namespace glance
{

MetaWheel::MetaWheel(ParkedWindows &parking, HoverPreviews &previews)
    : m_parking(parking)
    , m_previews(previews)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(wheelSettle);
    connect(&m_settle, &QTimer::timeout, this, &MetaWheel::settle);
    connect(&m_previews, &HoverPreviews::closing, this, [this](Window *window) {
        if (m_window == window) {
            m_settle.stop();
            m_window = nullptr;
        }
    });
}

// Meta+wheel (vertical) over a window grows it (scrolling up) or shrinks
// it, anchored at the pointer: the point under it stays put. In main the
// app is really resized at once. In a stash it is drawn larger or
// smaller (between just above parking size and full size), and the app
// gets the new size once the scrolling stops (wheelSettle). Over a
// parking icon it sizes the icon's preview (see resizePreview). Returns
// whether the event was taken.
bool MetaWheel::axis(PointerAxisEvent *event)
{
    if (event->modifiers != Qt::MetaModifier || event->orientation != Qt::Vertical || event->delta == 0
        || event->buttons != Qt::NoButton || workspace()->moveResizeWindow() || waylandServer()->seat()->isDrag()) {
        return false;
    }
    Window *window = m_parking.pick(event->position);
    if (!window || !manageable(window)) {
        return false;
    }
    const qreal step = event->source == PointerAxisSource::Wheel ? wheelStepWheel : wheelStepFinger;
    const qreal factor = std::exp(-event->delta * step);
    auto *it = m_parking.find(window);
    if (!it) {
        resizeInMain(window, factor, event->position);
    } else if (it->restoring) {
        return true; // on its way somewhere: wait
    } else if (m_parking.isParkingArea(m_parking.areaOf(window))) {
        resizePreview(window, factor, event->position);
    } else {
        resizeInStash(window, factor, event->position);
    }
    return true;
}

void MetaWheel::resizeInMain(Window *window, qreal factor, const QPointF &pos)
{
    const RectF frame = window->moveResizeGeometry();
    const RectF area = workspace()->clientArea(MaximizeArea, window);
    const QSizeF appMin = window->clientSizeToFrameSize(window->minSize());
    const QSizeF from(frame.width(), frame.height());
    // Both sides by the same factor, each within its own limits.
    const QSizeF to(std::clamp(from.width() * factor, std::max(wheelMinWidth, appMin.width()), std::max(area.width(), appMin.width())),
                    std::clamp(from.height() * factor, std::max(wheelMinHeight, appMin.height()), std::max(area.height(), appMin.height())));
    if (std::abs(to.width() - from.width()) < 0.5 && std::abs(to.height() - from.height()) < 0.5) {
        return;
    }
    releaseKdeState(window);
    const QRectF target = keptIn(QRectF(scaledTopLeft(QPointF(frame.x(), frame.y()), pos, from, to), to), area);
    window->moveResize(RectF(target.x(), target.y(), target.width(), target.height()));
}

void MetaWheel::resizeInStash(Window *window, qreal factor, const QPointF &pos)
{
    Parked &parked = m_parking.at(window);
    const qreal scale = parked.shown.width() / parked.original.width();
    const qreal want = std::clamp(scale * factor, parkingScale(parked.original) + 0.03, parkBelow - 0.01);
    if (std::abs(want - scale) < 1e-4) {
        return;
    }
    if (m_window && m_window != window) {
        settle();
    }
    const QSizeF size = parked.shown.size() * (want / scale);
    const QRectF from = m_parking.displayRect(parked);
    parked.shown = keptIn(QRectF(scaledTopLeft(parked.shown.topLeft(), pos, parked.shown.size(), size), size),
                          window->output()->geometryF());
    if (parked.preview) { // a small stashed window can be previewed
        parked.preview.reset();
        m_previews.suppress(window);
    }
    m_parking.animate(window, from);
    m_window = window;
    m_settle.start();
}

// The scrolling stopped: a stashed window's app is resized to fit (see
// park); a preview grown past its app's size gets the app resized to
// it, so it stays sharp (until the preview closes, see HoverPreviews).
void MetaWheel::settle()
{
    m_settle.stop();
    if (Window *window = m_window; window && m_parking.isParkedNotRestoring(window)) {
        const Parked parked = m_parking.at(window);
        if (!m_parking.isParkingArea(m_parking.areaOf(window))) {
            m_parking.park(window, parked.shown, parked.original);
        } else if (parked.preview && parked.preview->width() > window->frameGeometry().width() + 0.5) {
            window->moveResize(RectF(parked.preview->topLeft(), parked.preview->size().toSize()));
        }
    }
    m_window = nullptr;
}

// Over a parking icon: its hover preview grows or shrinks (opening at
// once if it isn't open), anchored at its screen edge and vertically at
// the pointer, from icon size up to the width of the edge zone (so it
// covers the stash but never main), the screen height, and the
// original size; clips (whose layout is their own) up to 1:1. It is
// still a preview: it closes when the pointer leaves, so nothing stays
// in the way. Shrunk back to icon size it closes, and doesn't open
// again until the pointer leaves the icon. Grown past the app's size,
// the app follows once the scrolling stops (settle).
void MetaWheel::resizePreview(Window *window, qreal factor, const QPointF &pos)
{
    Parked &parked = m_parking.at(window);
    const RectF screen = window->output()->geometryF();
    const RectF area = workspace()->clientArea(MaximizeArea, window);
    const RectF frame = window->frameGeometry();
    const QRectF current = parked.preview ? *parked.preview : parked.shown;
    const qreal aspect = current.height() / current.width();
    const qreal minWidth = parked.shown.width();
    const qreal maxWidth = std::max(minWidth, isClip(window) ? frame.width()
                                                             : std::min({screen.width() * zoneFraction, area.height() / aspect,
                                                                         parked.original.width()}));
    const qreal width = std::clamp(current.width() * factor, minWidth, maxWidth);
    if (std::abs(width - current.width()) < 0.5) {
        return;
    }
    m_previews.stopTimers();
    if (width <= minWidth + 0.5) {
        if (parked.preview) {
            m_previews.close(window);
        }
        m_previews.suppress(window);
        return;
    }
    if (m_window && m_window != window) {
        settle();
    }
    if (Window *old = m_previews.current(); old && old != window) {
        m_previews.close(old);
    }
    const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
    const QSizeF size(width, width * aspect);
    const qreal anchor = std::clamp((pos.y() - current.y()) / current.height(), 0.0, 1.0);
    const qreal y = std::clamp(pos.y() - anchor * size.height(), area.y(),
                               std::max(area.y(), area.y() + area.height() - size.height()));
    m_previews.show(window, QRectF(QPointF(left ? parked.shown.left() : parked.shown.right() - size.width(), y), size));
    if (!isClip(window)) {
        m_window = window;
        m_settle.start();
    }
}

} // namespace glance
