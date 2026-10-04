// Glance, a KWin effect: windows shrink as they are dragged toward the left or
// right screen edge, and stay shrunk ("parked") where they are dropped.
//
// Regions: main (the middle half of the screen, full size), stash (between
// main and the edge, smaller) and parking (the very edge, icon-sized).
// "Parked" means dropped while shrunk, in a stash or a parking area.
//
// Dragging: a window dragged toward the left or right edge shrinks; Meta+drag
// adds acceleration and pause-to-snap (see drag.h). Quick tiling by dragging
// to the side is turned off while the effect is loaded, since it uses the
// same edges.
//
// Parked: dropped while shrunk, the window stays exactly where and as large
// as it was drawn (its "shown" rectangle). The app is also really resized,
// down to a phone-like width (see layoutSize), so web pages reflow; the rest
// of the shrink is a transform on the window's scene item, which always fits
// the window's current frame into the shown rectangle, also while the app is
// still catching up with the new size. Dragged back into main, the window is
// resized to its original size.
//
// Input to parked windows: their frames are moved under the pointer so KWin
// finds what is drawn there, or we forward events ourselves; windows that
// act like icons can be clicked or dragged from anywhere (see
// parkedinput.h).
//
// It is a KWin effect (not a plain plugin) so that it can mark scaled windows
// as transformed in prePaintWindow: otherwise KWin clips a window's drawing
// as if it weren't scaled, and it also treats the window as still covering
// its full-size area.
//
// Keyboard: Meta+arrows move the active window between places,
// Meta+Alt+arrows select a window (see keyboard.h).
//
// Focus ring: the active window gets an outline that bounces when the ring
// moves by keyboard (see focusring.h).
//
// Previews: hovering a parking icon grows it in place (see previews.h).
//
// Declutter (Meta+double-click): the window takes a half of main, the
// others go to the stashes; again undoes it (see declutter.h).
//
// Clips: text or images dropped on the desktop, or clipped with Meta+C,
// become clip windows that drag back into apps (see clips.h).
//
// Stacks: the windows in each parking area form one column, centered
// vertically, in the order of their vertical position (see arrangeArea).
// Whenever a window arrives (keyboard or drop) or leaves (keyboard, dragged
// out, closed), the column re-forms, animated. Stashes have no column:
// windows stay where they were put and may overlap (a keyboard move keeps
// the window's height); only declutter lines a stash up. Crowding of
// parking (a column taller than the screen) comes later.
//
// Meta+wheel resizes the window under the pointer in place (see wheel.h).
//
// Alt+Tab (and Meta+Tab; replaces KDE's window switcher): hunt and return,
// and the desktop map while Alt is held (see alttab.h).
//
// Minimize = park: parking is Glance's minimize. A window being minimized
// (title-bar button, taskbar, shortcut, the app) is shown again at once and
// goes to the parking area on the side nearer to it (see
// ParkedWindows::minimizeToParking);
// focus moves on, as for a minimize.
//
// Known gaps: touch and tablets aren't handled.

#include <core/output.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <input.h>
#include <input_event.h>
#include <options.h>
#include <pointer_input.h>
#include <scene/outlinedborderitem.h>
#include <scene/windowitem.h>
#include <wayland/seat.h>
#include <wayland/surface.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include "alttab.h"
#include "clips.h"
#include "declutter.h"
#include "drag.h"
#include "focusring.h"
#include "geometry.h"
#include "kde.h"
#include "keyboard.h"
#include "parked.h"
#include "parkedinput.h"
#include "previews.h"
#include "wheel.h"

#include <QAction>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QPalette>
#include <QPluginLoader>
#include <QPointer>
#include <QTimer>
#include <QTransform>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <set>

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

    void postPaintScreen() override
    {
        if (m_drag.animating() || m_altTab.mapShown()) {
            effects->addRepaintFull();
        }
        if (m_parking.anyAnimating()) {
            effects->addRepaintFull();
        }
        Effect::postPaintScreen();
    }

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

    using Parked = ParkedWindows::Parked;
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
        connect(window, &Window::interactiveMoveResizeStepped, this, [this, window]() {
            m_drag.step(window);
        });
        connect(window, &Window::interactiveMoveResizeFinished, this, [this, window]() {
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
            m_keyboard.closed(window);
        });
    }
};

KWIN_EFFECT_FACTORY(Glance, "metadata.json")

#include "main.moc"
