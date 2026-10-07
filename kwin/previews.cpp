// Hover previews (see previews.h).
#include "previews.h"

#include <core/output.h>
#include <wayland/seat.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <algorithm>

using namespace KWin;

namespace glance
{

HoverPreviews::HoverPreviews(ParkedWindows &parking)
    : m_parking(parking)
{
    m_open.setSingleShot(true);
    m_open.setInterval(previewDelay);
    connect(&m_open, &QTimer::timeout, this, [this]() {
        if (m_candidate && isPreviewable(m_candidate)) {
            open(m_candidate);
        }
    });
    m_close.setSingleShot(true);
    m_close.setInterval(previewGrace);
    connect(&m_close, &QTimer::timeout, this, [this]() {
        if (Window *window = current()) {
            close(window);
        }
    });
}

// The previewed window, if it still is one.
Window *HoverPreviews::current()
{
    if (m_preview && !(m_parking.isParked(m_preview) && m_parking.at(m_preview).preview)) {
        m_preview = nullptr;
    }
    return m_preview;
}

// The parking icon whose home spot in its column is at `pos` (the spot counts
// also while that window is out as a preview), half the gap around it
// included so moving along the column never falls between two.
Window *HoverPreviews::iconAt(const QPointF &pos) const
{
    for (const auto &[window, parked] : m_parking.all()) {
        if (isPreviewable(window) && !window->isMinimized() && window->isOnCurrentDesktop()
            && parked.shown.adjusted(0, -arrangeGap / 2, 0, arrangeGap / 2).contains(pos)) {
            return window;
        }
    }
    return nullptr;
}

// Previews are for parking icons only: a stashed window can be as small
// as an icon (dropped near the edge) but isn't one to preview.
bool HoverPreviews::isPreviewable(Window *window) const
{
    return m_parking.isIcon(window) && m_parking.isParkingArea(m_parking.areaOf(window));
}

// On every pointer motion: the icon whose home spot is under the pointer
// grows after previewDelay, or at once if another is grown already (that
// one shrinks at the same time), also where the grown one covers that
// spot. Elsewhere over the grown one it stays; anywhere else it shrinks
// after previewGrace. Nothing changes while a button is held, a window is
// moved, or something is dragged.
void HoverPreviews::update(const QPointF &pos, Qt::MouseButtons buttons)
{
    if (buttons != Qt::NoButton || workspace()->moveResizeWindow() || waylandServer()->seat()->isDrag()) {
        m_open.stop();
        return;
    }
    Window *preview = current();
    Window *icon = iconAt(pos);
    if (m_noPreview && icon != m_noPreview) {
        m_noPreview = nullptr;
    }
    if (icon && icon == m_noPreview) {
        m_open.stop();
        m_close.stop();
        return;
    }
    if (!icon && preview && m_parking.drawnContains(preview, pos)) {
        m_open.stop();
        m_close.stop();
        return;
    }
    if (!icon) {
        m_open.stop();
        m_candidate = nullptr;
        if (preview && !m_close.isActive()) {
            m_close.start();
        }
        return;
    }
    m_close.stop();
    if (icon == preview) {
        m_open.stop();
    } else if (preview) {
        m_open.stop();
        open(icon);
    } else if (icon != m_candidate || !m_open.isActive()) {
        m_candidate = icon;
        m_open.start();
    }
}

// In place: previewGrow times its home spot (at most 1:1 with the app's
// current layout, and the screen height), at its screen edge, centered
// vertically on its spot.
QRectF HoverPreviews::previewRect(Window *window) const
{
    const Parked &parked = m_parking.at(window);
    const RectF screen = window->output()->geometryF();
    const RectF area = workspace()->clientArea(MaximizeArea, window);
    const RectF frame = window->frameGeometry();
    const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
    const qreal scale = std::min({previewGrow * parked.shown.width() / frame.width(), 1.0,
                                  area.height() / frame.height()});
    const QSizeF size = QSizeF(frame.width(), frame.height()) * scale;
    const qreal x = left ? parked.shown.left() : parked.shown.right() - size.width();
    const qreal y = std::clamp(parked.shown.center().y() - size.height() / 2, area.y(),
                               std::max(area.y(), area.y() + area.height() - size.height()));
    return QRectF(QPointF(x, y), size);
}

void HoverPreviews::open(Window *window)
{
    if (Window *old = current(); old && old != window) {
        close(old);
    }
    m_candidate = nullptr;
    const QRectF from = m_parking.currentlyDrawn(window);
    m_parking.at(window).preview = previewRect(window);
    m_preview = window;
    workspace()->raiseWindow(window);
    m_parking.animate(window, from);
}

void HoverPreviews::close(Window *window)
{
    const QRectF from = m_parking.currentlyDrawn(window);
    Parked &parked = m_parking.at(window);
    parked.preview.reset();
    if (window == m_preview) {
        m_preview = nullptr;
    }
    shrinkApp(window);
    m_parking.animate(window, from);
}

// A preview enlarged with Meta+wheel may have resized the app (see
// MetaWheel::settle): back to its parked layout size.
void HoverPreviews::shrinkApp(Window *window)
{
    Q_EMIT closing(window);
    const Parked &parked = m_parking.at(window);
    const RectF frame = window->frameGeometry();
    if (isClip(window) || parked.restoring) {
        return;
    }
    const QSizeF layout = m_parking.layoutSize(window, parked.shown.size(), parked.original);
    if (layout.width() < frame.width() - 0.5) {
        window->moveResize(RectF(parked.shown.topLeft(), layout));
    }
}

void HoverPreviews::show(Window *window, const QRectF &rect)
{
    const QRectF from = m_parking.currentlyDrawn(window);
    m_parking.at(window).preview = rect;
    m_preview = window;
    m_candidate = nullptr;
    m_noPreview = nullptr;
    workspace()->raiseWindow(window);
    m_parking.animate(window, from);
}

void HoverPreviews::stopTimers()
{
    m_open.stop();
    m_close.stop();
}

void HoverPreviews::suppress(Window *window)
{
    m_noPreview = window;
}

} // namespace glance
