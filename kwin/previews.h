// Hover previews: hovering a parking icon grows it in place, over its
// neighbours, still an icon (drag anywhere, clicks pass through)
// (docs/hover-previews.md). Meta+wheel sizes a preview (MetaWheel).
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
