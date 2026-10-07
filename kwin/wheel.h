// Meta+wheel resizes the window under the pointer in place; over a
// parking icon it sizes the icon's hover preview (docs/meta-wheel.md).
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
