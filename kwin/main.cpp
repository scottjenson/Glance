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
// as it was drawn (its "shown" rectangle). The app is also really resized,
// down to a phone-like width (see layoutSize), so web pages reflow; the rest
// of the shrink is a transform on the window's scene item, which always fits
// the window's current frame into the shown rectangle, also while the app is
// still catching up with the new size. Dragged back into the middle, the
// window is resized to its original size.
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
// Tiny parked windows (below iconBelow of their original size) act like
// icons: a left-button press on one is held back.
// Dragging it more than a few pixels moves the window (KWin's own move, so
// it grows back out of the edge zone); releasing it without dragging passes
// the press and release to the app as a click. So small parked windows can
// be dragged from anywhere and still work as widgets (buttons, scrolling).
// Not for KDE title bars (KWin handles those) or presses with modifiers.
//
// Keyboard (replaces KDE's quick tiling on Meta+arrows): Meta+Left/Right
// step the active window between left parking lot, left staging, left half
// of the middle, right half of the middle, right staging and right parking
// lot; a free window in the middle first snaps to the half on that side.
// Windows move horizontally and keep their vertical position. Meta+Up makes
// a window fill the screen height; Meta+Down undoes that.
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
#include <cmath>
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
        // Back to normal: full size, where they are drawn.
        for (auto &[window, parked] : m_parked) {
            if (window->windowItem()) {
                window->windowItem()->setTransform(QTransform());
            }
            window->moveResize(RectF(parked.shown.topLeft(), parked.original));
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

    // Meta+arrows (see the header comment). Keys we act on are not passed
    // on, so KDE's own quick tiling on them doesn't run.
    bool onKey(KeyboardKeyEvent *event)
    {
        if (event->modifiers != Qt::MetaModifier) {
            return false;
        }
        const Qt::Key key = event->key;
        if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down) {
            return false;
        }
        Window *window = workspace()->activeWindow();
        if (!window || !window->isNormalWindow() || window->isFullScreen() || !window->isMovable()
            || !window->isResizable() || workspace()->moveResizeWindow() || !window->windowItem()) {
            return false;
        }
        if (event->state == KeyboardKeyState::Pressed) {
            switch (key) {
            case Qt::Key_Left:
                stepSideways(window, Side::Left);
                break;
            case Qt::Key_Right:
                stepSideways(window, Side::Right);
                break;
            case Qt::Key_Up:
                fillHeight(window);
                break;
            default:
                undoFillHeight(window);
                break;
            }
        }
        return true;
    }

    bool onMotion(PointerMotionEvent *event)
    {
        if (m_pending) {
            return pendingMotion(event);
        }
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
        if (pressed && holdPress(event)) {
            return true;
        }
        if (!pressed && m_pending && event->button == Qt::LeftButton) {
            return releasePending(event);
        }
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
        bool keyboardKey(KeyboardKeyEvent *event) override { return m_effect->onKey(event); }
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
    // Parked windows drawn smaller than this (relative to the original size)
    // act like icons: drag anywhere to move, click passes through. Larger
    // ones stay normal windows, so their content keeps every interaction.
    static constexpr qreal iconBelow = 0.25;
    // How far (logical pixels) a press on an icon must move to become a drag.
    static constexpr qreal dragThreshold = 6.0;
    // Dropped at this scale or larger, a window goes back to full size.
    static constexpr qreal parkBelow = 0.99;
    // On parking, the app is resized no narrower than this (keeping its
    // shape), so web pages switch to their phone layout.
    static constexpr qreal minLayoutWidth = 400.0;

    // A parked window, or one being resized back to its original size.
    struct Parked
    {
        // Where the frame is drawn (global coordinates). Its width sets the
        // scale; the height follows the frame's shape.
        QRectF shown;
        // Frame size before the window was first parked.
        QSizeF original;
        // Being resized back to `original`; done once it has that size.
        bool restoring = false;
    };
    std::map<Window *, Parked> m_parked;

    // The window being dragged while we draw it scaled, its original size,
    // and its scale relative to that.
    QPointer<Window> m_dragged;
    QSizeF m_dragOriginal;
    qreal m_dragScale = 1.0;

    // Where we last pointed the seat, and whether KWin disagreed (so we
    // forward events ourselves).
    QPointer<Window> m_target;
    bool m_forwarding = false;

    // Frame y and height of windows before Meta+Up, for Meta+Down.
    std::map<Window *, std::pair<qreal, qreal>> m_beforeFillHeight;

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
            m_beforeFillHeight.erase(window);
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
        Window *window = pick(event->position);
        if (!window || !isParked(window)) {
            return false;
        }
        const Parked &parked = m_parked.at(window);
        if (parked.restoring || parked.shown.width() / parked.original.width() >= iconBelow) {
            return false;
        }
        // Line the frame up with the pointer, so KWin sees what's under it.
        route(event->position, false);
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
        route(event->position, false); // points the seat at the window
        auto seat = waylandServer()->seat();
        seat->setTimestamp(press.timestamp);
        seat->notifyPointerButton(press.nativeButton, PointerButtonState::Pressed);
        seat->notifyPointerFrame();
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerButton(event->nativeButton, PointerButtonState::Released);
        seat->notifyPointerFrame();
        return true;
    }

    // --- Keyboard: stepping between places ---

    enum class Side { Left, Right };
    // Where a window is, for Meta+Left/Right. Each side has a parking lot, a
    // staging area and a half of the middle; Free is anywhere else.
    enum class Place { ParkedLeft, StagingLeft, HalfLeft, HalfRight, StagingRight, ParkedRight, Free };

    // Scale of a window in staging when put there with the keyboard.
    static constexpr qreal stagingScale = 0.5;

    Place placeOf(Window *window) const
    {
        const RectF screen = window->output()->geometryF();
        const qreal zoneWidth = screen.width() * zoneFraction;
        auto it = m_parked.find(window);
        if (it != m_parked.end() && !it->second.restoring) {
            const Parked &parked = it->second;
            const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
            const bool tiny = parked.shown.width() / parked.original.width() < minScale + 0.02;
            if (tiny) {
                return left ? Place::ParkedLeft : Place::ParkedRight;
            }
            return left ? Place::StagingLeft : Place::StagingRight;
        }
        const RectF frame = window->moveResizeGeometry();
        auto near = [](qreal a, qreal b) {
            return std::abs(a - b) < 2.0;
        };
        if (near(frame.width(), zoneWidth)) {
            if (near(frame.x(), screen.x() + zoneWidth)) {
                return Place::HalfLeft;
            }
            if (near(frame.x(), screen.x() + 2 * zoneWidth)) {
                return Place::HalfRight;
            }
        }
        return Place::Free;
    }

    // One step towards `side` along: parked L, staging L, half L, half R,
    // staging R, parked R. A free window goes to the half on that side.
    void stepSideways(Window *window, Side side)
    {
        static constexpr Place order[] = {Place::ParkedLeft, Place::StagingLeft, Place::HalfLeft,
                                          Place::HalfRight, Place::StagingRight, Place::ParkedRight};
        const Place from = placeOf(window);
        Place to;
        if (from == Place::Free) {
            to = side == Side::Left ? Place::HalfLeft : Place::HalfRight;
        } else {
            const int i = int(std::find(std::begin(order), std::end(order), from) - std::begin(order));
            const int j = std::clamp(i + (side == Side::Left ? -1 : 1), 0, 5);
            if (i == j) {
                return;
            }
            to = order[j];
        }
        moveTo(window, to);
    }

    void moveTo(Window *window, Place place)
    {
        // KDE's own maximized or tiled state would fight our geometry.
        if (window->maximizeMode() != MaximizeRestore) {
            window->maximize(MaximizeRestore);
        }
        if (window->quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
            window->setQuickTileModeAtCurrentPosition(QuickTileFlag::None);
        }
        m_beforeFillHeight.erase(window);

        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const qreal zoneWidth = screen.width() * zoneFraction;

        // Its full (unparked) size, and the vertical center it keeps.
        auto it = m_parked.find(window);
        const bool parked = it != m_parked.end();
        const RectF current = window->moveResizeGeometry();
        const QSizeF size = parked ? it->second.original : QSizeF(current.width(), current.height());
        const qreal centerY = parked ? it->second.shown.center().y() : current.y() + current.height() / 2;
        auto topFor = [&](qreal height) {
            return std::clamp(centerY - height / 2, area.y(), std::max(area.y(), area.y() + area.height() - height));
        };

        switch (place) {
        case Place::HalfLeft:
        case Place::HalfRight: {
            const qreal height = std::min(size.height(), area.height());
            const qreal x = screen.x() + (place == Place::HalfLeft ? zoneWidth : 2 * zoneWidth);
            const RectF target(QPointF(x, topFor(height)), QSizeF(zoneWidth, height));
            if (parked) {
                // Leaving a parked state: keep drawing it scaled into the
                // target until the app has its new size.
                m_parked[window] = Parked{.shown = target, .original = target.size(), .restoring = true};
                window->moveResize(target);
                applyParked(window);
            } else {
                window->moveResize(target);
            }
            break;
        }
        case Place::StagingLeft:
        case Place::StagingRight:
        case Place::ParkedLeft:
        case Place::ParkedRight: {
            const bool staging = place == Place::StagingLeft || place == Place::StagingRight;
            const qreal scale = staging ? stagingScale : minScale;
            const QSizeF drawn = size * scale;
            // Where the drag rule gives this scale: the outer edge this far in.
            const qreal depth = (scale - minScale) / (1.0 - minScale) * zoneWidth;
            const bool left = place == Place::StagingLeft || place == Place::ParkedLeft;
            const qreal x = left ? screen.x() + depth : screen.x() + screen.width() - depth - drawn.width();
            park(window, QRectF(QPointF(x, topFor(drawn.height())), drawn), size);
            break;
        }
        case Place::Free:
            break;
        }
    }

    // Meta+Up: fill the screen height, keeping width and x.
    void fillHeight(Window *window)
    {
        if (isParked(window)) {
            return;
        }
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const RectF current = window->moveResizeGeometry();
        if (!m_beforeFillHeight.contains(window)) {
            m_beforeFillHeight[window] = {current.y(), current.height()};
        }
        window->moveResize(RectF(current.x(), area.y(), current.width(), area.height()));
    }

    // Meta+Down: back to the height before Meta+Up.
    void undoFillHeight(Window *window)
    {
        auto it = m_beforeFillHeight.find(window);
        if (it == m_beforeFillHeight.end() || isParked(window)) {
            return;
        }
        const RectF current = window->moveResizeGeometry();
        window->moveResize(RectF(current.x(), it->second.first, current.width(), it->second.second));
        m_beforeFillHeight.erase(it);
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
        const RectF frame = window->frameGeometry();
        if (m_dragged != window) {
            // Start of a drag. The scale is relative to the original size,
            // which a parked (resized) window remembers.
            auto it = m_parked.find(window);
            m_dragOriginal = it != m_parked.end() ? it->second.original : QSizeF(frame.width(), frame.height());
            m_parked.erase(window);
            m_dragged = window;
        }

        const QPointF cursor = input()->pointer()->pos();
        const RectF screen = window->moveResizeOutput()->geometryF();
        const qreal left = frame.x();
        const qreal right = frame.x() + frame.width();
        // From the current frame size to the original size.
        const qreal grow = m_dragOriginal.width() / frame.width();

        // Full size while the window stays within the middle of the screen;
        // shrinks as its left or right edge goes into the edge zone.
        const qreal zoneWidth = screen.width() * zoneFraction;
        qreal total = std::min({1.0,
                                edgeScale(cursor.x() - screen.x(), (cursor.x() - left) * grow, zoneWidth),
                                edgeScale(screen.x() + screen.width() - cursor.x(), (right - cursor.x()) * grow, zoneWidth)});
        total = std::max(total, minScale);
        m_dragScale = total;
        // The scale to draw the current frame at.
        const qreal scale = total * grow;

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

        const RectF frame = window->frameGeometry();
        const QRectF drawn = window->windowItem()->transform()
                                 .mapRect(QRectF(0, 0, frame.width(), frame.height()))
                                 .translated(frame.topLeft());

        if (m_dragScale < parkBelow) {
            park(window, drawn, m_dragOriginal);
        } else if (m_dragOriginal != QSizeF(frame.width(), frame.height())) {
            // Back to the original size, keeping the grabbed spot under the
            // cursor: the drawing grows around it until the app has resized.
            const QPointF cursor = input()->pointer()->pos();
            const qreal grow = m_dragOriginal.width() / frame.width();
            const QRectF target(cursor - (cursor - frame.topLeft()) * grow, m_dragOriginal);
            m_parked[window] = Parked{.shown = target, .original = m_dragOriginal, .restoring = true};
            qInfo("edge-shrink: %s: restore to %.0fx%.0f", qPrintable(window->caption()),
                  m_dragOriginal.width(), m_dragOriginal.height());
            window->moveResize(RectF(target.topLeft(), m_dragOriginal));
            applyParked(window);
        } else {
            window->windowItem()->setTransform(QTransform());
        }
    }

    // Park a window: draw it at `shown`, and really resize the app (see
    // layoutSize). `original` is its size before it was first parked.
    void park(Window *window, const QRectF &shown, const QSizeF &original)
    {
        m_parked[window] = Parked{.shown = shown, .original = original};
        const RectF frame = window->frameGeometry();
        const QSizeF layout = layoutSize(window, shown.size(), original);
        if (layout != QSizeF(frame.width(), frame.height())) {
            qInfo("edge-shrink: %s: original %.0fx%.0f, app minimum %.0fx%.0f, shown %.0fx%.0f -> resize to %.0fx%.0f",
                  qPrintable(window->caption()), original.width(), original.height(),
                  window->minSize().width(), window->minSize().height(),
                  shown.width(), shown.height(), layout.width(), layout.height());
            window->moveResize(RectF(shown.topLeft(), layout));
        }
        applyParked(window);
    }

    // The size to really resize a parked window to, when it is shown at
    // `shown`. For clean text, prefer laying out at exactly the shown size
    // (not scaled at all) or twice it (drawn at 1/2, which averages exact
    // 2x2 pixel blocks). Otherwise the smallest size that keeps the shape and
    // meets both minLayoutWidth and the app's minimum, capped at `original`.
    static QSizeF layoutSize(Window *window, const QSizeF &shown, const QSizeF &original)
    {
        const QSizeF appMin = window->clientSizeToFrameSize(window->minSize());
        auto fits = [&](const QSizeF &size) {
            return size.width() >= minLayoutWidth && size.width() >= appMin.width()
                && size.height() >= appMin.height() && size.width() <= original.width();
        };
        for (const qreal ratio : {1.0, 2.0}) {
            const QSizeF size = (shown * ratio).toSize();
            if (fits(size)) {
                return size;
            }
        }
        const qreal k = std::max({minLayoutWidth / shown.width(), appMin.width() / shown.width(),
                                  appMin.height() / shown.height()});
        const QSizeF size = (shown * k).toSize();
        return size.width() > original.width() ? original : size;
    }

    // --- Parked windows ---

    bool isParked(Window *window) const
    {
        return m_parked.contains(window);
    }

    // The scale a parked window's current frame is drawn at.
    qreal scaleOf(Window *window) const
    {
        return m_parked.at(window).shown.width() / window->frameGeometry().width();
    }

    // Draw a parked window at its place, whatever its frame's current
    // position and size. A restoring window is done once it has its
    // original size; then it just needs to be where it is drawn.
    void applyParked(Window *window)
    {
        auto it = m_parked.find(window);
        if (it == m_parked.end() || !window->windowItem()) {
            return;
        }
        const Parked &parked = it->second;
        const RectF frame = window->frameGeometry();
        if (parked.restoring && std::abs(frame.width() - parked.original.width()) < 0.5
            && std::abs(frame.height() - parked.original.height()) < 0.5) {
            const QPointF topLeft = parked.shown.topLeft();
            m_parked.erase(it);
            window->windowItem()->setTransform(QTransform());
            if (frame.topLeft() != topLeft) {
                window->move(topLeft);
            }
            return;
        }

        const qreal scale = scaleOf(window);
        const QPointF offset = parked.shown.topLeft() - frame.topLeft();
        QTransform transform;
        transform.translate(offset.x(), offset.y());
        transform.scale(scale, scale);
        window->windowItem()->setTransform(transform);
    }

    // Where a parked window is drawn.
    QRectF drawnRect(Window *window) const
    {
        const auto frame = window->frameGeometry();
        return QRectF(m_parked.at(window).shown.topLeft(), QSizeF(frame.width(), frame.height()) * scaleOf(window));
    }

    // Move a parked window's frame so that the point of the window drawn at
    // `pos` is also at `pos` in the frame. The drawing stays in place.
    void reanchor(Window *window, const QPointF &pos)
    {
        if (workspace()->moveResizeWindow() == window) {
            return;
        }
        const QPointF drawn = m_parked.at(window).shown.topLeft();
        const QPointF topLeft = pos - (pos - drawn) / scaleOf(window);
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

        const QPointF drawn = it->second.shown.topLeft();
        const qreal scale = scaleOf(window);
        const QPointF frame = window->frameGeometry().topLeft();
        const QPointF buffer = window->bufferGeometry().topLeft();
        QMatrix4x4 m;
        m.translate(frame.x() - buffer.x(), frame.y() - buffer.y());
        m.scale(1.0 / scale, 1.0 / scale);
        m.translate(-drawn.x(), -drawn.y());
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
            syncSeatFocus(pos);
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

KWIN_EFFECT_FACTORY(EdgeShrink, "metadata.json")

#include "main.moc"
