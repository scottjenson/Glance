// Focus ring: an accent-colored outline on the highlighted window (the
// active one, or the Alt+Tab selection), as wide on screen at any scale;
// a small bounce when it moves by keyboard (docs/focus-ring.md).
#pragma once

#include "parked.h"

#include <QObject>
#include <QPointer>

namespace KWin
{
class Item;
class OutlinedBorderItem;
class WindowPaintData;
class WindowPrePaintData;
}

namespace glance
{

class AltTab;

class FocusRing : public QObject
{
    Q_OBJECT

public:
    FocusRing(ParkedWindows &parking, AltTab &altTab);
    ~FocusRing() override;

    // Outline the highlighted window (again).
    void update();
    // The ring goes to `window` by keyboard: it bounces there.
    void bounce(Window *window);

    // Window events: the ring follows its window's frame, and goes before
    // the window does.
    void frameChanged(Window *window);
    void fullScreenChanged(Window *window);
    void closed(Window *window);

    // The ring's scene item, if it is shown (a child of its window's).
    KWin::Item *ring() const;

    // Painting a bouncing window.
    bool bouncing() const;
    void prePaintWindow(Window *window, KWin::WindowPrePaintData &data);
    void paintWindow(Window *window, KWin::WindowPaintData &data);

private:
    void remove();
    void startBounce(Window *window);

    ParkedWindows &m_parking;
    AltTab &m_altTab;
    // The ring (see update) and the window it outlines.
    KWin::OutlinedBorderItem *m_ring = nullptr;
    QPointer<Window> m_window;
    // The window bouncing as it gets the ring, its current scale, and
    // which bounce it is (later timers of an earlier one do nothing).
    QPointer<Window> m_bounce;
    qreal m_bounceScale = 1.0;
    int m_bounceCount = 0;
};

} // namespace glance
