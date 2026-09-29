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
// Known gaps, fine for this test: server-side decorations (title bars drawn
// by KWin) of a shrunk window don't get input; touch and tablets aren't
// handled; the cursor shape in the dead zone may be the shrunk window's.

#include <input.h>
#include <input_event.h>
#include <effect/effect.h>
#include <effect/effectwindow.h>
#include <main.h>
#include <pointer_input.h>
#include <scene/windowitem.h>
#include <wayland/seat.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <QMatrix4x4>
#include <QPointer>
#include <QTransform>

#include <map>

using namespace KWin;

class EdgeShrinkTest : public Effect
{
public:
    EdgeShrinkTest()
        : m_filter(this)
    {
        input()->installInputEventFilter(&m_filter);
        qInfo("edge-shrink: test plugin loaded; Alt+Shift+S toggles the active window");
    }

    ~EdgeShrinkTest() override
    {
        input()->uninstallInputEventFilter(&m_filter);
        for (auto &[window, scale] : m_shrunk) {
            if (window && window->windowItem()) {
                window->windowItem()->setTransform(QTransform());
            }
        }
    }

    // Only take part in painting while something is shrunk.
    bool isActive() const override
    {
        return !m_shrunk.empty();
    }

    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override
    {
        if (scaleOf(w->window())) {
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

    // Shrunk windows and their scale (removed when a window closes).
    std::map<Window *, qreal> m_shrunk;
    // Where we last pointed the seat, and whether KWin disagreed (so we
    // forward events ourselves).
    QPointer<Window> m_target;
    bool m_forwarding = false;

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
