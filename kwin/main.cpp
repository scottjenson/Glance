// edge-shrink for KWin: feasibility test.
//
// Question: can a window that is drawn shrunk still receive clicks, scrolling
// and hover at the right spot? KWin maps pointer positions into a window with
// a plain offset (Window::inputTransformation()), so on its own it would
// deliver them as if the window were full size.
//
// Alt+Shift+S shrinks the active window to half size (visually, anchored at
// its top-left corner) or restores it. While shrunk:
// - Drawing: a scale transform on the window's scene item. This is a KWin
//   effect (not a plain plugin) only so it can mark shrunk windows as
//   transformed in prePaintWindow: otherwise KWin clips a window's drawing
//   to its region as if it weren't scaled, and only the top-left quarter of
//   a half-size window gets painted.
// - Input: KWin still picks the pointer's window from the window's real,
//   full-size rectangle. Over the visible (shrunk) part it picks the right
//   window, so we only replace the seat's pointer transformation with one
//   that includes the scale, and KWin forwards events as usual. Over the
//   invisible rest of the real rectangle ("dead zone") it picks the wrong
//   window, so there we point the seat at whatever is really visible under
//   the pointer and forward the events ourselves.
//
// Dragging (step 1 of the port): while KWin moves a window interactively
// (title bar or Meta+drag), the window is drawn shrunk around the cursor as it
// nears the left/right screen edge, with the same curve as the Wayfire
// plugin. Drawing only: on release it goes back to full size. Quick tiling
// by dragging to the side is turned off while the effect is loaded, since it
// uses the same edges.
//
// Known gaps, fine for this test: server-side decorations (title bars drawn
// by KWin) of a shrunk window don't get input; touch and tablets aren't
// handled; the cursor shape in the dead zone may be the shrunk window's.

#include <input.h>
#include <input_event.h>
#include <effect/effect.h>
#include <effect/effectwindow.h>
#include <core/output.h>
#include <main.h>
#include <options.h>
#include <pointer_input.h>
#include <scene/windowitem.h>
#include <wayland/seat.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <QMatrix4x4>
#include <QPointer>
#include <QTransform>

#include <algorithm>
#include <map>

using namespace KWin;

class EdgeShrinkTest : public Effect
{
public:
    EdgeShrinkTest()
        : m_filter(this)
    {
        input()->installInputEventFilter(&m_filter);

        for (Window *window : workspace()->windows()) {
            watch(window);
        }
        connect(workspace(), &Workspace::windowAdded, this, &EdgeShrinkTest::watch);

        m_savedTiling = options->electricBorderTiling();
        options->setElectricBorderTiling(false);
        // Keep it off if the settings are reloaded.
        connect(options, &Options::electricBorderTilingChanged, this, []() {
            if (options->electricBorderTiling()) {
                options->setElectricBorderTiling(false);
            }
        });

        qInfo("edge-shrink: test plugin loaded; Alt+Shift+S toggles the active window");
    }

    ~EdgeShrinkTest() override
    {
        input()->uninstallInputEventFilter(&m_filter);
        disconnect(options, nullptr, this, nullptr);
        options->setElectricBorderTiling(m_savedTiling);
        if (m_dragged && m_dragged->windowItem()) {
            m_dragged->windowItem()->setTransform(QTransform());
        }
        for (auto &[window, scale] : m_shrunk) {
            if (window && window->windowItem()) {
                window->windowItem()->setTransform(QTransform());
            }
        }
    }

    // Only take part in painting while something is shrunk.
    bool isActive() const override
    {
        return !m_shrunk.empty() || m_dragged;
    }

    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override
    {
        if (scaleOf(w->window()) || w->window() == m_dragged) {
            data.setTransformed();
        }
        Effect::prePaintWindow(view, w, data);
    }

    bool onKey(KeyboardKeyEvent *event)
    {
        if (event->key != Qt::Key_S || event->modifiers != (Qt::AltModifier | Qt::ShiftModifier)) {
            return false;
        }

        if (event->state == KeyboardKeyState::Pressed) {
            toggle(workspace()->activeWindow());
        }
        return true;
    }

    bool onMotion(PointerMotionEvent *event)
    {
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
        // Keep the target of a press until its release.
        if (!route(event->position, !pressed || event->buttons != event->button)) {
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
        explicit Filter(EdgeShrinkTest *effect)
            : InputEventFilter(InputFilterOrder::ButtonRebind)
            , m_effect(effect)
        {
        }
        bool keyboardKey(KeyboardKeyEvent *event) override { return m_effect->onKey(event); }
        bool pointerMotion(PointerMotionEvent *event) override { return m_effect->onMotion(event); }
        bool pointerButton(PointerButtonEvent *event) override { return m_effect->onButton(event); }
        bool pointerAxis(PointerAxisEvent *event) override { return m_effect->onAxis(event); }

    private:
        EdgeShrinkTest *m_effect;
    };

    Filter m_filter;

    // --- Tuning knobs ---
    // Width of the left and right edge zones, where shrinking happens, as a
    // fraction of the screen width: the middle half stays full size.
    static constexpr qreal zoneFraction = 0.25;
    // Window size at the very edge of the screen (1.0 = full size).
    static constexpr qreal minScale = 0.15;

    // The window being dragged while we draw it shrunk.
    QPointer<Window> m_dragged;
    // Quick tiling setting to restore when unloaded.
    bool m_savedTiling = true;

    // Shrunk windows and their scale (removed when a window closes).
    std::map<Window *, qreal> m_shrunk;
    // Where we last pointed the seat, and whether KWin disagreed (so we
    // forward events ourselves).
    QPointer<Window> m_target;
    bool m_forwarding = false;

    void watch(Window *window)
    {
        connect(window, &Window::interactiveMoveResizeStepped, this, [this, window]() {
            dragStep(window);
        });
        connect(window, &Window::interactiveMoveResizeFinished, this, [this, window]() {
            dragFinished(window);
        });
    }

    // How far a box spanning [x, x + width) must move horizontally to lie
    // inside `screen`. A box wider than the screen keeps its left edge visible.
    static qreal shiftOntoScreen(qreal x, qreal width, const RectF &screen)
    {
        const qreal minShift = screen.x() - x;
        const qreal maxShift = (screen.x() + screen.width()) - (x + width);
        if (minShift > maxShift) {
            return minShift;
        }
        return std::clamp(0.0, minShift, maxShift);
    }

    // Window scale for a cursor position: 1 in the middle of the screen,
    // falling linearly across an edge zone to minScale at the screen edge.
    static qreal scaleForPosition(qreal x, const RectF &screen)
    {
        const qreal toLeft = x - screen.x();
        const qreal toRight = (screen.x() + screen.width()) - x;
        const qreal distance = std::max(0.0, std::min(toLeft, toRight));
        const qreal zoneWidth = screen.width() * zoneFraction;
        if (distance >= zoneWidth) {
            return 1.0;
        }
        return minScale + (1.0 - minScale) * (distance / zoneWidth);
    }

    // KWin has moved the dragged window so the grabbed spot is under the
    // cursor; draw it scaled around the cursor.
    void dragStep(Window *window)
    {
        if (!window->isInteractiveMove() || scaleOf(window) || !window->windowItem()) {
            return;
        }
        m_dragged = window;

        const QPointF cursor = input()->pointer()->pos();
        const RectF screen = window->moveResizeOutput()->geometryF();
        const RectF frame = window->frameGeometry();
        const qreal left = frame.x();
        const qreal right = frame.x() + frame.width();

        qreal scale = scaleForPosition(cursor.x(), screen);
        // Shrink further if needed so that, scaled around the cursor, the
        // window still fits between the screen edges: its edge then rests
        // against the screen edge while the grabbed spot stays under the cursor.
        if (cursor.x() > left) {
            scale = std::min(scale, (cursor.x() - screen.x()) / (cursor.x() - left));
        }
        if (right > cursor.x()) {
            scale = std::min(scale, (screen.x() + screen.width() - cursor.x()) / (right - cursor.x()));
        }
        scale = std::max(scale, minScale);

        // Fallback when even minScale doesn't fit: slide it back on screen
        // (the cursor then detaches from the grabbed spot).
        const qreal drawnLeft = cursor.x() + (left - cursor.x()) * scale;
        const qreal shift = shiftOntoScreen(drawnLeft, frame.width() * scale, screen);

        // The item's coordinates start at the frame's top-left corner.
        const QPointF anchor = cursor - frame.topLeft();
        QTransform transform;
        transform.translate(anchor.x() + shift, anchor.y());
        transform.scale(scale, scale);
        transform.translate(-anchor.x(), -anchor.y());
        window->windowItem()->setTransform(transform);
    }

    void dragFinished(Window *window)
    {
        if (window != m_dragged) {
            return;
        }
        if (window->windowItem()) {
            window->windowItem()->setTransform(QTransform());
        }
        m_dragged = nullptr;
    }

    std::optional<qreal> scaleOf(Window *window) const
    {
        auto it = m_shrunk.find(window);
        if (it == m_shrunk.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Where a shrunk window is drawn: scaled around its top-left corner.
    static QRectF visibleRect(Window *window, qreal scale)
    {
        const auto frame = window->frameGeometry();
        return QRectF(frame.x(), frame.y(), frame.width() * scale, frame.height() * scale);
    }

    // Maps a global position to the window's surface-local position.
    QMatrix4x4 transformFor(Window *window) const
    {
        auto scale = scaleOf(window);
        if (!scale) {
            return window->inputTransformation();
        }

        const QPointF frame = window->frameGeometry().topLeft();
        const QPointF buffer = window->bufferGeometry().topLeft();
        QMatrix4x4 m;
        m.translate(frame.x() - buffer.x(), frame.y() - buffer.y());
        m.scale(1.0 / *scale, 1.0 / *scale);
        m.translate(-frame.x(), -frame.y());
        return m;
    }

    // The window really visible at `pos`, like InputRedirection::findToplevel
    // but using the drawn rectangle for shrunk windows.
    Window *pick(const QPointF &pos) const
    {
        const auto &stacking = workspace()->stackingOrder();
        for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
            Window *window = *it;
            if (window->isDeleted() || !window->isOnCurrentActivity() || !window->isOnCurrentDesktop()
                || window->isMinimized() || window->isHidden() || window->isHiddenByShowDesktop()
                || !window->readyForPainting()) {
                continue;
            }

            if (auto scale = scaleOf(window)) {
                if (visibleRect(window, *scale).contains(pos)) {
                    return window;
                }
                continue;
            }

            if (window->hitTest(pos)) {
                return window;
            }
        }
        return nullptr;
    }

    // Point the seat at the surface really under `pos`, with a transformation
    // that accounts for shrinking. Returns whether we must forward the event
    // ourselves because KWin's own pointer focus is wrong. With `keep`, a
    // button is held: stay with the current target.
    bool route(const QPointF &pos, bool keep)
    {
        if (m_shrunk.empty()) {
            return false;
        }

        auto pointer = input()->pointer();
        if (keep) {
            if (m_target && scaleOf(m_target)) {
                waylandServer()->seat()->setFocusedPointerSurfaceTransformation(transformFor(m_target));
            }
            return m_forwarding;
        }

        Window *target = pick(pos);
        const bool involved = (target && scaleOf(target)) || (pointer->hover() && scaleOf(pointer->hover()));
        if (!involved) {
            // Nothing shrunk here: KWin knows best (decorations etc.).
            m_target = nullptr;
            m_forwarding = false;
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

        m_target = target;
        m_forwarding = target != pointer->focus();
        return m_forwarding;
    }

    void toggle(Window *window)
    {
        if (!window || !window->windowItem()) {
            return;
        }

        if (m_shrunk.erase(window)) {
            disconnect(window, &Window::closed, this, nullptr);
            window->windowItem()->setTransform(QTransform());
            // KWin only recomputes the pointer transformation on enter or
            // geometry change, so the scaled one would otherwise stay.
            auto seat = waylandServer()->seat();
            if (window->surface() && seat->focusedPointerSurface() == window->surface()) {
                seat->setFocusedPointerSurfaceTransformation(window->inputTransformation());
            }
            m_target = nullptr;
            qInfo("edge-shrink: restored %s", qPrintable(window->caption()));
        } else {
            const qreal scale = 0.5;
            m_shrunk[window] = scale;
            connect(window, &Window::closed, this, [this, window]() {
                m_shrunk.erase(window);
            });
            window->windowItem()->setTransform(QTransform::fromScale(scale, scale));
            qInfo("edge-shrink: shrunk %s to %g", qPrintable(window->caption()), scale);
        }

        // Re-route for the current pointer position right away.
        auto pointer = input()->pointer();
        pointer->update();
        route(pointer->pos(), false);
        m_forwarding = false;
    }
};

KWIN_EFFECT_FACTORY(EdgeShrinkTest, "metadata.json")

#include "main.moc"
