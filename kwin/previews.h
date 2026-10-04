// Hover previews (docs/hover-previews.md): hovering a parking icon (not a
// small stashed window) makes it grow in place to previewGrow times its
// size (at most 1:1 with the app's resized layout, so it stays sharp),
// anchored at its screen edge and centered on its spot, over its
// neighbours, which stay put and partly visible. The pointer stays over
// it, so it can still be dragged, and clicks pass through as for any icon.
// The first waits previewDelay; moving into a neighbour's spot then
// switches at once (both animate); leaving closes it after previewGrace
// (see update). Meta+wheel sizes a preview (see MetaWheel::resizePreview).
#pragma once

#include "parked.h"

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>

namespace glance
{

class HoverPreviews : public QObject
{
    Q_OBJECT

public:
    explicit HoverPreviews(ParkedWindows &parking);

    // On every pointer motion (see the .cpp).
    void update(const QPointF &pos, Qt::MouseButtons buttons);
    // The previewed window, if it still is one (a preview reset elsewhere,
    // e.g. by a clip drag, is noticed here).
    Window *current();
    // For Meta+wheel (see MetaWheel::resizePreview): close a preview, show
    // `window`'s preview at `rect` (closing none: the caller does), stop
    // the timers that open and close previews, and keep the pointer from
    // previewing `window` again until it leaves it.
    void close(Window *window);
    void show(Window *window, const QRectF &rect);
    void stopTimers();
    void suppress(Window *window);

Q_SIGNALS:
    // A preview is closing: Meta+wheel's pending app resize for it is off
    // (the app goes back to its parked layout size).
    void closing(KWin::Window *window);

private:
    using Parked = ParkedWindows::Parked;

    Window *iconAt(const QPointF &pos) const;
    bool isPreviewable(Window *window) const;
    QRectF previewRect(Window *window) const;
    void open(Window *window);
    void shrinkApp(Window *window);

    ParkedWindows &m_parking;
    // The previewed window, the one the pointer waits on, and the timers to
    // open and close previews (see update).
    QPointer<Window> m_preview;
    QPointer<Window> m_candidate;
    QTimer m_open;
    QTimer m_close;
    // An icon whose preview Meta+wheel just closed: no hover preview for it
    // until the pointer leaves it.
    QPointer<Window> m_noPreview;
};

} // namespace glance
