// Glance, a KWin effect: windows shrink as they are dragged toward the left or
// right screen edge, and stay shrunk ("parked") where they are dropped.
//
// Regions: main (the middle half of the screen, full size), stash (between
// main and the edge, smaller) and parking (the very edge, icon-sized).
// "Parked" means dropped while shrunk, in a stash or a parking area.
//
// This file is the coordinator: the effect KWin loads. It owns the
// components, hands them window events and painting, and decides in one
// place which one gets an input event first (see onKey, onMotion,
// onButton, onAxis). The components, each with its own state; each gets
// references to the parts it needs:
// - ParkedWindows (parked.h): parked windows, their places, animation and
//   columns, and where every window is drawn. Everything builds on it.
// - ParkedInput (parkedinput.h): pointer input reaches the window drawn
//   under the pointer; icons are clicked or dragged from anywhere.
// - WindowDrag (drag.h): shrinking while dragging; Meta+drag acceleration
//   and pause-to-snap.
// - Keyboard (keyboard.h): Meta+arrows between places, Meta+Alt+arrows
//   selection.
// - AltTab (alttab.h): Alt+Tab hunt and return, the desktop map.
// - FocusRing (focusring.h): the outline on the active window, its bounce.
// - HoverPreviews (previews.h): parking icons grow in place on hover.
// - MetaWheel (wheel.h): Meta+wheel resizes in place.
// - Declutter (declutter.h): Meta+double-click.
// - Clips (clips.h): drops and Meta+C become clip windows.
// - KdeIntegration (kde.h): what Glance switches off in KDE while loaded.
//
// It is a KWin effect (not a plain plugin) so that it can mark scaled windows
// as transformed in prePaintWindow: otherwise KWin clips a window's drawing
// as if it weren't scaled, and it also treats the window as still covering
// its full-size area.
//
// Known gaps: touch and tablets aren't handled.

#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <input.h>
#include <input_event.h>
#include <window.h>
#include <workspace.h>

#include "alttab.h"
#include "clips.h"
#include "declutter.h"
#include "drag.h"
#include "focusring.h"
#include "kde.h"
#include "keyboard.h"
#include "parked.h"
#include "parkedinput.h"
#include "previews.h"
#include "wheel.h"

#include <QPointer>
#include <QTimer>

using namespace KWin;
using namespace glance;

class Glance : public Effect
{
public:
    Glance()
        : m_filter(this)
    {
        input()->installInputEventFilter(&m_filter);

        for (Window *window : workspace()->windows()) {
            watch(window);
        }
        connect(workspace(), &Workspace::windowAdded, this, &Glance::watch);
        connect(workspace(), &Workspace::windowAdded, &m_clips, &Clips::placeClip);

        qInfo("glance: effect loaded");
        if (!m_kde.globalAccel()) {
            qWarning("glance: KWin's kglobalaccel plugin not found: a Meta held long or used with Glance may open the launcher");
        }
    }

    ~Glance() override
    {
        input()->uninstallInputEventFilter(&m_filter);
        // Back to normal: full size, where they are drawn.
        m_parking.restoreAll();
    }

    // Only take part in painting while something is scaled.
    bool isActive() const override
    {
        return !m_parking.empty() || m_drag.window() || m_focusRing.bouncing() || m_altTab.mapShown() || m_clips.dragging();
    }

    // Direct scanout (a full-screen window, or a video on an overlay plane,
    // sent to the display without compositing) only while nothing is drawn
    // in paint hooks: KWin's scanout check knows item transforms (parked
    // windows at rest) but not what paintWindow changes (the bounce, the
    // map, a dragged clip) or what is about to move.
    bool blocksDirectScanout() const override
    {
        return m_drag.window() || m_focusRing.bouncing() || m_altTab.mapShown() || m_clips.dragging()
            || m_parking.anyAnimating();
    }

    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override
    {
        if (m_parking.isParked(w->window()) || w->window() == m_drag.window()) {
            data.setTransformed();
        }
        m_focusRing.prePaintWindow(w->window(), data);
        m_clips.prePaintWindow(w->window(), data);
        m_altTab.prePaintWindow(w->window(), data);
        Effect::prePaintWindow(view, w, data);
    }

    // A bouncing window (see FocusRing::paintWindow); then the Alt+Tab map
    // or a dragged clip.
    void paintWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask,
                     const Region &deviceRegion, WindowPaintData &data) override
    {
        m_focusRing.paintWindow(w->window(), data);
        if (m_altTab.paintWindow(w->window(), data)) {
            // The map paints the whole screen as transformed, and KWin then
            // gives each window an unlimited region. With that, its renderer
            // cuts windows off at the screen's edge as if they weren't
            // scaled (clipQuads in scene/itemrenderer_opengl.cpp uses only
            // the translation), so shrunk windows lose their right and
            // bottom parts. A finite region makes it clip on the GPU instead,
            // which is right.
            Effect::paintWindow(renderTarget, viewport, w, mask, Region(viewport.deviceRect()), data);
            return;
        }
        if (m_clips.paintWindow(w->window(), data)) {
            // A clip being dragged is drawn under the pointer. The whole
            // screen is painted as transformed meanwhile, so every window
            // needs a finite region (see above).
            Effect::paintWindow(renderTarget, viewport, w, mask, Region(viewport.deviceRect()), data);
            return;
        }
        Effect::paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
    }

    // The Alt+Tab label, over everything.
    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const Region &deviceRegion,
                     LogicalOutput *screen) override
    {
        Effect::paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
        m_altTab.paintScreen(renderTarget, viewport, screen);
    }

    // Advance animations: each frame, redraw animating windows at their
    // current place; finished ones settle.
    void prePaintScreen(ScreenPrePaintData &data) override
    {
        m_parking.advance();
        m_drag.prePaintScreen();
        m_clips.prePaintScreen(data);
        m_altTab.prePaintScreen(data);
        Effect::prePaintScreen(data);
    }

    // Ask for the next frame only where something still moves: changing a
    // window's transform repaints just what it covered and covers now.
    void postPaintScreen() override
    {
        m_parking.scheduleFrames();
        m_drag.postPaintScreen();
        m_altTab.postPaintScreen();
        Effect::postPaintScreen();
    }

    // Input, in the order the components get it. While Alt+Tab is on, the
    // pointer does nothing and keys go to it. A dragged clip follows the
    // pointer. A held icon press decides between click and drag before
    // anything else sees motion. Meta+double-click, then drops (clips),
    // then icon presses get buttons; Meta+wheel gets the wheel. Whatever
    // is left goes to ParkedInput, which delivers it to the window drawn
    // under the pointer (or leaves it to KWin when nothing parked is
    // involved). Returning true means we took the event.
    bool onKey(KeyboardKeyEvent *event)
    {
        if (event->key == Qt::Key_Meta || event->key == Qt::Key_Super_L || event->key == Qt::Key_Super_R) {
            m_kde.metaKey(event);
        }
        if (m_drag.window()) {
            // Meta pressed or released during a drag: switch between gesture
            // and normal drag.
            m_drag.modifiersChanged();
        }
        if (m_altTab.switching() || (!m_input.pressPending() && m_altTab.startsSwitch(event))) {
            return m_altTab.key(event);
        }
        return m_keyboard.key(event);
    }

    bool onMotion(PointerMotionEvent *event)
    {
        if (m_altTab.switching() || m_altTab.mapShown()) {
            return true; // the pointer does nothing while switching
        }
        if (m_clips.dragging()) {
            m_clips.follow(event->position);
            return false; // KWin's drag and drop goes on
        }
        if (m_input.pressPending()) {
            return m_input.pendingMotion(event);
        }
        m_previews.update(event->position, event->buttons);
        return m_input.motion(event);
    }

    bool onButton(PointerButtonEvent *event)
    {
        const bool pressed = event->state == PointerButtonState::Pressed;
        if (pressed && (m_altTab.switching() || m_altTab.mapShown())) {
            return true;
        }
        if (pressed) {
            // Where a drag that may follow started (KWin's own move anchor
            // follows the cursor, so it can't tell us).
            m_lastPress = event->position;
        }
        if (m_declutter.button(event)) {
            return true;
        }
        if (!pressed && m_clips.drop(event)) {
            return true;
        }
        if (pressed && m_input.holdPress(event)) {
            return true;
        }
        if (!pressed && m_input.pressPending() && event->button == Qt::LeftButton) {
            return m_input.releasePending(event);
        }
        return m_input.button(event);
    }

    bool onAxis(PointerAxisEvent *event)
    {
        if (m_altTab.switching() || m_altTab.mapShown()) {
            return true;
        }
        if (m_wheel.axis(event)) {
            return true;
        }
        return m_input.axis(event);
    }

private:
    // Effect has its own pointerMotion() etc., so the input filter is a
    // separate object that hands events back to us.
    class Filter : public InputEventFilter
    {
    public:
        explicit Filter(Glance *effect)
            : InputEventFilter(InputFilterOrder::ButtonRebind)
            , m_effect(effect)
        {
        }
        bool keyboardKey(KeyboardKeyEvent *event) override { return m_effect->onKey(event); }
        bool pointerMotion(PointerMotionEvent *event) override { return m_effect->onMotion(event); }
        // KDE's "Meta alone opens the launcher" is called off by a click or
        // scroll during the Meta press, but only in a later filter: one we
        // take must call it off here (see cancelMetaTap).
        bool pointerButton(PointerButtonEvent *event) override
        {
            const bool taken = m_effect->onButton(event);
            if (taken && event->state == PointerButtonState::Pressed && event->modifiers != Qt::NoModifier) {
                m_effect->m_kde.cancelMetaTap();
            }
            return taken;
        }
        bool pointerAxis(PointerAxisEvent *event) override
        {
            const bool taken = m_effect->onAxis(event);
            if (taken && event->modifiers != Qt::NoModifier) {
                m_effect->m_kde.cancelMetaTap();
            }
            return taken;
        }

    private:
        Glance *m_effect;
    };

    Filter m_filter;
    KdeIntegration m_kde;

    ParkedWindows m_parking;
    // Where the last pointer button press was: where a clip drag grabbed
    // the clip.
    QPointF m_lastPress;
    Clips m_clips{m_parking, m_lastPress};
    AltTab m_altTab{m_parking};
    Declutter m_declutter{m_parking};
    HoverPreviews m_previews{m_parking};
    MetaWheel m_wheel{m_parking, m_previews};
    FocusRing m_focusRing{m_parking, m_altTab};
    Keyboard m_keyboard{m_parking, m_focusRing};
    WindowDrag m_drag{m_parking};
    ParkedInput m_input{m_parking};

    void watch(Window *window)
    {
        connect(window, &Window::interactiveMoveResizeStarted, this, [this, window]() {
            if (window->isInteractiveResize()) {
                m_parking.resizeStarted(window);
            }
        });
        connect(window, &Window::interactiveMoveResizeStepped, this, [this, window]() {
            m_drag.step(window);
        });
        connect(window, &Window::interactiveMoveResizeFinished, this, [this, window]() {
            m_parking.resizeFinished(window);
            m_drag.finished(window);
        });
        connect(window, &Window::frameGeometryChanged, this, [this, window]() {
            m_clips.frameChanged(window);
            m_parking.applyParked(window);
            m_focusRing.frameChanged(window);
        });
        connect(window, &Window::minimizedChanged, this, [this, window]() {
            if (window->isMinimized()) {
                m_parking.minimizeToParking(window);
            }
        });
        if (window->isMinimized()) {
            // Already minimized when we see it (at load, or a window that
            // starts minimized): once it is set up.
            QTimer::singleShot(0, this, [this, window = QPointer<Window>(window)]() {
                if (window && window->isMinimized()) {
                    m_parking.minimizeToParking(window);
                }
            });
        }
        connect(window, &Window::fullScreenChanged, this, [this, window]() {
            m_focusRing.fullScreenChanged(window);
        });
        connect(window, &Window::closed, this, [this, window]() {
            m_focusRing.closed(window); // while the window's item still exists
            m_clips.closed(window);
            m_parking.closed(window);
        });
    }
};

KWIN_EFFECT_FACTORY(Glance, "metadata.json")

#include "glance.moc"
