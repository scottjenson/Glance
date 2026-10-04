// Meta+wheel (docs/meta-wheel.md) resizes the window under the pointer in
// place, anchored at the pointer: in main a real resize, in a stash a
// scaled one (the app follows when the scrolling stops). Over a parking
// icon it sizes the icon's hover preview, up to the width of the edge
// zone; the preview still closes when the pointer leaves (see
// resizePreview).
#pragma once

#include "parked.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

namespace KWin
{
struct PointerAxisEvent;
}

namespace glance
{

class HoverPreviews;

class MetaWheel : public QObject
{
    Q_OBJECT

public:
    MetaWheel(ParkedWindows &parking, HoverPreviews &previews);

    // Every pointer axis event. Returns whether it was taken.
    bool axis(KWin::PointerAxisEvent *event);

private:
    using Parked = ParkedWindows::Parked;

    void resizeInMain(Window *window, qreal factor, const QPointF &pos);
    void resizeInStash(Window *window, qreal factor, const QPointF &pos);
    void settle();
    void resizePreview(Window *window, qreal factor, const QPointF &pos);

    ParkedWindows &m_parking;
    HoverPreviews &m_previews;
    // The stashed window or preview being resized, and the timer that
    // resizes the app once the scrolling stops.
    QPointer<Window> m_window;
    QTimer m_settle;
};

} // namespace glance
