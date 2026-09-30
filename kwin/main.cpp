// edge-shrink for KWin: windows shrink as they are dragged toward the left or
// right screen edge, and stay shrunk ("parked") where they are dropped.
//
// Dragging: while KWin moves a window interactively (title bar or Meta+drag),
// the window is drawn shrunk around the cursor once its left or right edge
// goes into the outer quarter of the screen (the middle half is full size),
// reaching minScale at the screen edge. Quick tiling by dragging to the side
// is turned off while the effect is loaded, since it uses the same edges.
//
// Parking: dropped while shrunk, the window stays exactly where and as large
// as it was drawn. KWin still thinks it is full size, at its real ("frame")
// position; only the drawing is scaled (a transform on its scene item).
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
// Known gaps: touch and tablets aren't handled; in the forwarding case the
// title bar doesn't respond and the cursor shape may be wrong.

#include <core/output.h>
#include <effect/effect.h>
#include <effect/effectwindow.h>
#include <input.h>
#include <input_event.h>
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

class EdgeShrink : public Effect
{
public:
    EdgeShrink()
        : m_filter(this)
    {
        input()->installInputEventFilter(&m_filter);

        for (Window *window : workspace()->windows()) {
            watch(window);
        }
        connect(workspace(), &Workspace::windowAdded, this, &EdgeShrink::watch);

        m_savedTiling = options->electricBorderTiling();
        options->setElectricBorderTiling(false);
        // Keep it off if the settings are reloaded.
        connect(options, &Options::electricBorderTilingChanged, this, []() {
            if (options->electricBorderTiling()) {
                options->setElectricBorderTiling(false);
            }
        });

        qInfo("edge-shrink: effect loaded");
    }

    ~EdgeShrink() override
    {
        input()->uninstallInputEventFilter(&m_filter);
        disconnect(options, nullptr, this, nullptr);
        options->setElectricBorderTiling(m_savedTiling);
        if (m_dragged && m_dragged->windowItem()) {
            m_dragged->windowItem()->setTransform(QTransform());
        }
        for (auto &[window, parked] : m_parked) {
            if (window->windowItem()) {
                window->windowItem()->setTransform(QTransform());
            }
        }
    }

    // Only take part in painting while something is scaled.
    bool isActive() const override
    {
        return !m_parked.empty() || m_dragged;
    }

    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override
    {
        if (isParked(w->window()) || w->window() == m_dragged) {
            data.setTransformed();
        }
        Effect::prePaintWindow(view, w, data);
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
        explicit Filter(EdgeShrink *effect)
            : InputEventFilter(InputFilterOrder::ButtonRebind)
            , m_effect(effect)
        {
        }
        bool pointerMotion(PointerMotionEvent *event) override { return m_effect->onMotion(event); }
        bool pointerButton(PointerButtonEvent *event) override { return m_effect->onButton(event); }
        bool pointerAxis(PointerAxisEvent *event) override { return m_effect->onAxis(event); }

    private:
        EdgeShrink *m_effect;
    };

    Filter m_filter;

    // --- Tuning knobs ---
    // Width of the left and right edge zones, where shrinking happens, as a
    // fraction of the screen width: the middle half stays full size.
    static constexpr qreal zoneFraction = 0.25;
    // Window size at the very edge of the screen (1.0 = full size).
    static constexpr qreal minScale = 0.15;
    // Dropped at this scale or larger, a window goes back to full size.
    static constexpr qreal parkBelow = 0.99;

    // A parked window: drawn at `scale`, with its frame's top-left corner
    // drawn at `drawn` (global coordinates).
    struct Parked
    {
        qreal scale;
        QPointF drawn;
    };
    std::map<Window *, Parked> m_parked;

    // The window being dragged while we draw it scaled, and its scale.
    QPointer<Window> m_dragged;
    qreal m_dragScale = 1.0;

    // Where we last pointed the seat, and whether KWin disagreed (so we
    // forward events ourselves).
    QPointer<Window> m_target;
    bool m_forwarding = false;

    // Quick tiling setting to restore when unloaded.
    bool m_savedTiling = true;

    void watch(Window *window)
    {
        connect(window, &Window::interactiveMoveResizeStepped, this, [this, window]() {
            dragStep(window);
        });
        connect(window, &Window::interactiveMoveResizeFinished, this, [this, window]() {
            dragFinished(window);
        });
        connect(window, &Window::frameGeometryChanged, this, [this, window]() {
            applyParked(window);
        });
        connect(window, &Window::closed, this, [this, window]() {
            m_parked.erase(window);
        });
    }

    // --- Dragging ---

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

    // Scale at which a window, scaled around the cursor, has its edge on one
    // side at the point of the curve: full size until that edge enters the
    // edge zone, then falling linearly to minScale at the screen edge.
    // `cursorToScreenEdge` and `cursorToWindowEdge` are measured towards the
    // same side, the latter at full size. The drawn edge is at distance
    // d = cursorToScreenEdge - cursorToWindowEdge * s from the screen edge,
    // and s = minScale + (1 - minScale) * d / zoneWidth; solved for s:
    static qreal edgeScale(qreal cursorToScreenEdge, qreal cursorToWindowEdge, qreal zoneWidth)
    {
        const qreal k = (1.0 - minScale) / zoneWidth;
        return (minScale + k * cursorToScreenEdge) / (1.0 + k * cursorToWindowEdge);
    }

    // KWin has moved the dragged window so the grabbed spot is under the
    // cursor; draw it scaled around the cursor. A parked window being dragged
    // stops being parked: its frame was re-anchored around the cursor when
    // it was grabbed, so the drag continues from where it is drawn.
    void dragStep(Window *window)
    {
        if (!window->isInteractiveMove() || !window->windowItem()) {
            return;
        }
        m_parked.erase(window);
        m_dragged = window;

        const QPointF cursor = input()->pointer()->pos();
        const RectF screen = window->moveResizeOutput()->geometryF();
        const RectF frame = window->frameGeometry();
        const qreal left = frame.x();
        const qreal right = frame.x() + frame.width();

        // Full size while the window stays within the middle of the screen;
        // shrinks as its left or right edge goes into the edge zone.
        const qreal zoneWidth = screen.width() * zoneFraction;
        qreal scale = std::min({1.0,
                                edgeScale(cursor.x() - screen.x(), cursor.x() - left, zoneWidth),
                                edgeScale(screen.x() + screen.width() - cursor.x(), right - cursor.x(), zoneWidth)});
        scale = std::max(scale, minScale);
        m_dragScale = scale;

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

    // Dropped: park it where it is drawn, or restore full size.
    void dragFinished(Window *window)
    {
        if (window != m_dragged) {
            return;
        }
        m_dragged = nullptr;
        if (!window->windowItem()) {
            return;
        }

        if (m_dragScale < parkBelow) {
            const QPointF drawn = window->frameGeometry().topLeft()
                + window->windowItem()->transform().map(QPointF(0, 0));
            m_parked[window] = Parked{m_dragScale, drawn};
            applyParked(window);
        } else {
            window->windowItem()->setTransform(QTransform());
        }
    }

    // --- Parked windows ---

    bool isParked(Window *window) const
    {
        return m_parked.contains(window);
    }

    // Draw a parked window at its place, wherever its frame currently is.
    void applyParked(Window *window)
    {
        auto it = m_parked.find(window);
        if (it == m_parked.end() || !window->windowItem()) {
            return;
        }
        const Parked &parked = it->second;
        const QPointF offset = parked.drawn - window->frameGeometry().topLeft();
        QTransform transform;
        transform.translate(offset.x(), offset.y());
        transform.scale(parked.scale, parked.scale);
        window->windowItem()->setTransform(transform);
    }

    // Where a parked window is drawn.
    QRectF drawnRect(Window *window) const
    {
        const Parked &parked = m_parked.at(window);
        const auto frame = window->frameGeometry();
        return QRectF(parked.drawn, QSizeF(frame.width(), frame.height()) * parked.scale);
    }

    // Move a parked window's frame so that the point of the window drawn at
    // `pos` is also at `pos` in the frame. The drawing stays in place.
    void reanchor(Window *window, const QPointF &pos)
    {
        if (workspace()->moveResizeWindow() == window) {
            return;
        }
        const Parked &parked = m_parked.at(window);
        const QPointF topLeft = pos - (pos - parked.drawn) / parked.scale;
        if (topLeft != window->frameGeometry().topLeft()) {
            window->move(topLeft);
        }
    }

    // Maps a global position to the window's surface-local position.
    QMatrix4x4 transformFor(Window *window) const
    {
        auto it = m_parked.find(window);
        if (it == m_parked.end()) {
            return window->inputTransformation();
        }

        const Parked &parked = it->second;
        const QPointF frame = window->frameGeometry().topLeft();
        const QPointF buffer = window->bufferGeometry().topLeft();
        QMatrix4x4 m;
        m.translate(frame.x() - buffer.x(), frame.y() - buffer.y());
        m.scale(1.0 / parked.scale, 1.0 / parked.scale);
        m.translate(-parked.drawn.x(), -parked.drawn.y());
        return m;
    }

    // The window really visible at `pos`, like InputRedirection::findToplevel
    // but using the drawn rectangle for parked windows.
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

            if (isParked(window)) {
                if (drawnRect(window).contains(pos)) {
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

    // Make the pointer event at `pos` reach the window really visible there.
    // Returns whether we must forward it ourselves because KWin's own pick is
    // wrong. With `keep`, a button is held: stay with the current target.
    bool route(const QPointF &pos, bool keep)
    {
        // KWin's own drags (and no parked windows) need nothing from us.
        if (m_parked.empty() || workspace()->moveResizeWindow()) {
            m_target = nullptr;
            m_forwarding = false;
            return false;
        }

        auto pointer = input()->pointer();
        if (keep) {
            if (m_target && isParked(m_target)) {
                reanchor(m_target, pos);
                if (m_forwarding) {
                    waylandServer()->seat()->setFocusedPointerSurfaceTransformation(transformFor(m_target));
                }
            }
            return m_forwarding;
        }

        Window *target = pick(pos);
        const bool involved = (target && isParked(target)) || (pointer->hover() && isParked(pointer->hover()));
        if (!involved) {
            // Nothing parked here: KWin knows best.
            m_target = nullptr;
            m_forwarding = false;
            return false;
        }

        if (target && isParked(target)) {
            reanchor(target, pos);
            // KWin picked its window before the frame moved; pick again.
            pointer->update();
        }

        m_target = target;
        m_forwarding = target != pointer->hover();
        if (!m_forwarding) {
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

KWIN_EFFECT_FACTORY(EdgeShrink, "metadata.json")

#include "main.moc"
