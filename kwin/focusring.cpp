// Focus ring (see focusring.h).
#include "focusring.h"

#include "alttab.h"

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <scene/outlinedborderitem.h>
#include <scene/windowitem.h>
#include <window.h>
#include <workspace.h>

#include <QGuiApplication>
#include <QPalette>
#include <QTimer>

using namespace KWin;

namespace glance
{

FocusRing::FocusRing(ParkedWindows &parking, AltTab &altTab)
    : m_parking(parking)
    , m_altTab(altTab)
{
    // The ring keeps its width on screen whatever the window's scale.
    connect(&m_parking, &ParkedWindows::transformChanged, this, [this](Window *window) {
        if (window == m_window) {
            update();
        }
    });
    connect(&m_altTab, &AltTab::ringChanged, this, &FocusRing::update);
    connect(&m_altTab, &AltTab::bounce, this, &FocusRing::bounce);
    connect(workspace(), &Workspace::windowActivated, this, &FocusRing::update);
    update();
}

FocusRing::~FocusRing()
{
    remove();
}

// Outline the highlighted window, ringWidth wide on screen (also in the
// Alt+Tab map). A child of the window's scene item, so it moves, scales
// and stacks with it.
void FocusRing::update()
{
    Window *window = m_altTab.highlighted();
    const bool wanted = window && !window->isDeleted() && (window->isNormalWindow() || window->isDialog())
        && !window->isFullScreen() && window->windowItem();
    if (!wanted || window != m_window) {
        remove();
    }
    if (!wanted) {
        return;
    }
    const RectF frame = window->frameGeometry();
    const RectF inner(0, 0, frame.width(), frame.height());
    const qreal scale = window->windowItem()->transform().m11() * m_altTab.mapZoom(window);
    const QColor color = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
    const BorderOutline outline(ringWidth / (scale > 0 ? scale : 1.0), color, window->borderRadius());
    if (m_ring) {
        m_ring->setInnerRect(inner);
        m_ring->setOutline(outline);
        return;
    }
    m_ring = new OutlinedBorderItem(inner, outline, window->windowItem());
    m_ring->setZ(1000); // above the window's surfaces and title bar
    m_window = window;
}

KWin::Item *FocusRing::ring() const
{
    return m_ring;
}

// The ring moved by keyboard (Alt+Tab, Meta+Alt+arrows): it bounces there.
void FocusRing::bounce(Window *window)
{
    update();
    if (window && window == m_window) {
        startBounce(window);
    }
}

// Items don't delete their children, and a child must go before its
// parent: called at the latest when the window closes.
void FocusRing::remove()
{
    delete m_ring;
    m_ring = nullptr;
    m_window = nullptr;
}

// Step through bounceFrames, one every bounceStep (see paintWindow).
void FocusRing::startBounce(Window *window)
{
    const int count = ++m_bounceCount;
    m_bounce = window;
    m_bounceScale = bounceFrames[0];
    const int frames = int(std::size(bounceFrames));
    for (int i = 1; i < frames; ++i) {
        QTimer::singleShot(i * bounceStep, this, [this, count, i, frames]() {
            if (count != m_bounceCount || !m_bounce) {
                return;
            }
            m_bounceScale = bounceFrames[i];
            // Scaled down around its center, it stays within its own
            // bounds (the ring and shadow included): repaint just those.
            if (m_bounce->windowItem()) {
                m_bounce->windowItem()->scheduleRepaint(m_bounce->windowItem()->boundingRect());
            }
            if (i == frames - 1) {
                m_bounce = nullptr;
            }
        });
    }
}

void FocusRing::frameChanged(Window *window)
{
    if (window == m_window) {
        update();
    }
}

void FocusRing::fullScreenChanged(Window *window)
{
    if (window == m_altTab.highlighted()) {
        update();
    }
}

void FocusRing::closed(Window *window)
{
    if (window == m_window) {
        remove(); // while its parent item still exists
    }
}

bool FocusRing::bouncing() const
{
    return m_bounce;
}

void FocusRing::prePaintWindow(Window *window, WindowPrePaintData &data)
{
    if (window == m_bounce && m_bounceScale != 1.0) {
        data.setTransformed();
    }
}

// A bouncing window: scaled to its current frame's scale around the center
// of where it is drawn. The paint data's scale works around the window
// item's origin (it comes before the item's position), so the translation
// moves that center back.
void FocusRing::paintWindow(Window *window, WindowPaintData &data)
{
    if (window == m_bounce && m_bounceScale != 1.0) {
        const QPointF center = m_parking.currentlyDrawn(m_bounce).center() - m_bounce->windowItem()->position();
        data.setXScale(data.xScale() * m_bounceScale);
        data.setYScale(data.yScale() * m_bounceScale);
        data.translate(center.x() * (1.0 - m_bounceScale), center.y() * (1.0 - m_bounceScale));
    }
}

} // namespace glance
