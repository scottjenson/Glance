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
// Input to a parked window: whenever the pointer is over one, its frame is
// moved ("re-anchored") so that the point under the pointer is the same point
// of the window as in the shrunk drawing; the drawing is adjusted so it
// doesn't move. KWin then finds the right spot on its own, at the pointer:
// clicks, hover, the title bar (so it can be dragged out again), popups.
// Where KWin still picks another window (the invisible full-size frame of a
// different window lies above), we point the seat at the window really
// visible under the pointer and forward the events ourselves.
//
// It is a KWin effect (not a plain plugin) so that it can mark scaled windows
// as transformed in prePaintWindow: otherwise KWin clips a window's drawing
// as if it weren't scaled, and it also treats the window as still covering
// its full-size area.
//
// Windows in parking, and stashed ones below iconBelow of their original
// size, act like icons (see isIcon): a left-button press on one is held back.
// Dragging it more than a few pixels moves the window (KWin's own move, so
// it grows back out of the edge zone); releasing it without dragging passes
// the press and release to the app as a click. So small parked windows can
// be dragged from anywhere and still work as widgets (buttons, scrolling).
// Not for KDE title bars (KWin handles those) or presses with modifiers.
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
// Known gaps: touch and tablets aren't handled; in the forwarding case the
// title bar doesn't respond and the cursor shape may be wrong.

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

        m_anchorLate.setSingleShot(true);
        connect(&m_anchorLate, &QTimer::timeout, this, &Glance::anchorLate);

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
        if (m_altTab.switching() || (!m_pending && m_altTab.startsSwitch(event))) {
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
        if (m_pending) {
            return pendingMotion(event);
        }
        m_previews.update(event->position, event->buttons);
        if (!route(event->position, event->buttons != Qt::NoButton)) {
            return false;
        }

        auto seat = waylandServer()->seat();
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerMotion(event->position);
        return true;
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
        if (pressed && holdPress(event)) {
            return true;
        }
        if (!pressed && m_pending && event->button == Qt::LeftButton) {
            return releasePending(event);
        }
        // Keep the target of a press until its release.
        if (!route(event->position, !pressed || event->buttons != event->button, true)) {
            return false;
        }

        if (pressed && m_target) {
            workspace()->activateWindow(m_target);
        }

        auto seat = waylandServer()->seat();
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerButton(event->nativeButton, event->state);
        return true;
    }

    bool onAxis(PointerAxisEvent *event)
    {
        if (m_altTab.switching() || m_altTab.mapShown()) {
            return true;
        }
        if (m_wheel.axis(event)) {
            return true;
        }
        if (!route(event->position, event->buttons != Qt::NoButton)) {
            return false;
        }

        auto seat = waylandServer()->seat();
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerAxis(event->orientation, event->delta, event->deltaV120, event->source, event->inverted);
        return true;
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

    // Where we last pointed the seat, and whether KWin disagreed (so we
    // forward events ourselves).
    QPointer<Window> m_target;
    bool m_forwarding = false;
    // When a parked window's frame was last moved under the pointer, and
    // the timer that lines it up where the pointer stopped (see reanchor).
    std::chrono::steady_clock::time_point m_anchoredAt;
    QTimer m_anchorLate;

    // A left-button press on a parked window, held back until we know
    // whether it is a click or a drag.
    struct PendingPress
    {
        QPointer<Window> window;
        QPointF position;
        quint32 nativeButton;
        std::chrono::microseconds timestamp;
    };
    std::optional<PendingPress> m_pending;

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

    // --- Parked windows as icons: click or drag ---

    // A plain left press on an icon-like parked window (not on a KDE title
    // bar): hold it back. Returns whether it was held.
    bool holdPress(PointerButtonEvent *event)
    {
        if (event->button != Qt::LeftButton || event->buttons != Qt::LeftButton
            || event->modifiers != Qt::NoModifier || workspace()->moveResizeWindow()) {
            return false;
        }
        Window *window = m_parking.pick(event->position);
        // Clips get their presses: dragging a clip drags its text, and an
        // app can only start a drag from a press it received.
        if (!window || isClip(window) || !m_parking.isIcon(window)) {
            return false;
        }
        // Line the frame up with the pointer, so KWin sees what's under it.
        route(event->position, false, true);
        if (input()->pointer()->decoration()) {
            return false;
        }
        m_pending = PendingPress{window, event->position, event->nativeButton, event->timestamp};
        return true;
    }

    // While a press is held: far enough away, it becomes a move of the
    // window. Until then the app doesn't see the pointer move.
    bool pendingMotion(PointerMotionEvent *event)
    {
        const QPointF delta = event->position - m_pending->position;
        if (std::hypot(delta.x(), delta.y()) < dragThreshold) {
            return true;
        }
        const PendingPress press = *m_pending;
        m_pending.reset();
        if (press.window) {
            // The frame was anchored at the press position, so KWin's move
            // keeps the grabbed spot under the cursor. KWin then handles
            // this and the following motion and the release.
            press.window->performMousePressCommand(Options::MouseMove, press.position);
        }
        return false;
    }

    // Released without dragging: it was a click. Give the app the press and
    // this release.
    bool releasePending(PointerButtonEvent *event)
    {
        const PendingPress press = *m_pending;
        m_pending.reset();
        if (!press.window) {
            return true;
        }
        workspace()->activateWindow(press.window);
        route(event->position, false, true); // points the seat at the window
        auto seat = waylandServer()->seat();
        seat->setTimestamp(press.timestamp);
        seat->notifyPointerButton(press.nativeButton, PointerButtonState::Pressed);
        seat->notifyPointerFrame();
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerButton(event->nativeButton, PointerButtonState::Released);
        seat->notifyPointerFrame();
        return true;
    }

    // --- Drawing and input for managed windows ---

    // Move a parked window's frame so that the point of the window drawn at
    // `pos` is also at `pos` in the frame. The drawing stays in place.
    // Returns whether they line up; if not, the caller forwards events with
    // transformFor, which is exact without moving anything.
    // Each move is a real geometry change in KWin (window rules, the
    // window's monitor, the app is told), so pointer motion moves the frame
    // at most once per refresh; `now` (buttons) moves it at once. When
    // motion skips a move, anchorLate lines the frame up a refresh later, so
    // where the pointer stops (tooltips, menus) the frame is right.
    // The frame swings far past the drawing: never so far that its centre
    // leaves the window's monitor, or KWin would give it to the next one.
    bool reanchor(Window *window, const QPointF &pos, bool now)
    {
        if (workspace()->moveResizeWindow() == window) {
            return true;
        }
        const QPointF drawn = m_parking.displayRect(m_parking.at(window)).topLeft();
        const QPointF topLeft = pos - (pos - drawn) / m_parking.scaleOf(window);
        const RectF frame = window->frameGeometry();
        if (topLeft == frame.topLeft()) {
            return true;
        }
        if (!window->output()->geometryF().contains(frame.translated(topLeft - frame.topLeft()).center())) {
            return false;
        }
        const auto clock = std::chrono::steady_clock::now();
        const auto period = std::chrono::microseconds(1000000000 / std::max<uint32_t>(window->output()->refreshRate(), 1000));
        if (!now && clock < m_anchoredAt + period) {
            if (!m_anchorLate.isActive()) {
                m_anchorLate.start(std::chrono::ceil<std::chrono::milliseconds>(m_anchoredAt + period - clock));
            }
            return false;
        }
        m_anchorLate.stop();
        m_anchoredAt = clock;
        window->move(topLeft);
        return true;
    }

    // Motion skipped a move (see reanchor): line the frame up where the
    // pointer is now. Moving it makes KWin reset the seat's mapping for its
    // own pointer focus; when we point the seat ourselves, set ours again.
    void anchorLate()
    {
        if (!m_target || !m_parking.isParked(m_target) || m_pending || workspace()->moveResizeWindow()
            || waylandServer()->seat()->isDragPointer()) {
            return;
        }
        const QPointF pos = input()->pointer()->pos();
        if (!m_parking.drawnRect(m_target).contains(pos) || !reanchor(m_target, pos, true)) {
            return;
        }
        if (m_forwarding) {
            waylandServer()->seat()->setFocusedPointerSurfaceTransformation(transformFor(m_target));
        }
    }

    // Maps a global position to the window's surface-local position.
    QMatrix4x4 transformFor(Window *window) const
    {
        auto *it = m_parking.find(window);
        if (!it) {
            return window->inputTransformation();
        }

        const QPointF drawn = m_parking.displayRect(*it).topLeft();
        const qreal scale = m_parking.scaleOf(window);
        const QPointF frame = window->frameGeometry().topLeft();
        const QPointF buffer = window->bufferGeometry().topLeft();
        QMatrix4x4 m;
        m.translate(frame.x() - buffer.x(), frame.y() - buffer.y());
        m.scale(1.0 / scale, 1.0 / scale);
        m.translate(-drawn.x(), -drawn.y());
        return m;
    }

    // When we forward events ourselves, we point the seat at another surface
    // than KWin's focus window, and KWin doesn't notice: it only re-points
    // the seat when its own focus changes. Before leaving events to KWin
    // again, point the seat back at KWin's focus window, or KWin would
    // deliver them to whatever surface we last chose (e.g. the desktop).
    void syncSeatFocus(const QPointF &pos)
    {
        auto seat = waylandServer()->seat();
        Window *focus = input()->pointer()->focus();
        SurfaceInterface *surface = focus ? focus->surface() : nullptr;
        if (seat->focusedPointerSurface() == surface) {
            return;
        }
        if (surface) {
            seat->notifyPointerEnter(surface, pos, focus->inputTransformation());
        } else {
            seat->notifyPointerLeave();
        }
    }

    // Make the pointer event at `pos` reach the window really visible there.
    // Returns whether we must forward it ourselves because KWin's own pick is
    // wrong, or the frame isn't lined up (see reanchor). With `keep`, a
    // button is held: stay with the current target. `now`: a button event,
    // line the frame up at once.
    bool route(const QPointF &pos, bool keep, bool now = false)
    {
        // KWin's own moves, drag and drop (and no parked windows) need
        // nothing from us. During drag and drop, re-anchoring would move the
        // invisible full-size frame of the window the drag started in (a
        // clip) under the pointer, where it would catch the drop.
        if (m_parking.empty() || workspace()->moveResizeWindow() || waylandServer()->seat()->isDragPointer()) {
            m_target = nullptr;
            m_forwarding = false;
            return false;
        }

        auto pointer = input()->pointer();
        if (keep) {
            bool exact = true;
            if (m_target && m_parking.isParked(m_target)) {
                exact = reanchor(m_target, pos, now);
                if (m_forwarding || !exact) {
                    waylandServer()->seat()->setFocusedPointerSurfaceTransformation(transformFor(m_target));
                }
            }
            return m_forwarding || !exact;
        }

        Window *target = m_parking.pick(pos);
        const bool involved = (target && m_parking.isParked(target)) || (pointer->hover() && m_parking.isParked(pointer->hover()));
        if (!involved) {
            // Nothing parked here: KWin knows best.
            m_target = nullptr;
            m_forwarding = false;
            syncSeatFocus(pos);
            return false;
        }

        bool exact = true;
        if (target && m_parking.isParked(target)) {
            exact = reanchor(target, pos, now);
            // KWin picked its window before the frame moved; pick again.
            pointer->update();
        }

        m_target = target;
        m_forwarding = target != pointer->hover() || !exact;
        if (!m_forwarding) {
            syncSeatFocus(pos);
            return false;
        }

        auto seat = waylandServer()->seat();
        SurfaceInterface *surface = target ? target->surface() : nullptr;
        if (seat->focusedPointerSurface() != surface) {
            if (surface) {
                seat->notifyPointerEnter(surface, pos, transformFor(target));
            } else {
                seat->notifyPointerLeave();
            }
        } else if (surface) {
            seat->setFocusedPointerSurfaceTransformation(transformFor(target));
        }
        return true;
    }
};

KWIN_EFFECT_FACTORY(Glance, "metadata.json")

#include "main.moc"
