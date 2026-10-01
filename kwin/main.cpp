// Glance, a KWin effect: windows shrink as they are dragged toward the left or
// right screen edge, and stay shrunk ("parked") where they are dropped.
//
// Regions: main (the middle half of the screen, full size), stash (between
// main and the edge, smaller) and parking (the very edge, icon-sized).
// "Parked" means dropped while shrunk, in a stash or a parking area.
//
// Dragging: while KWin moves a window interactively (title bar or Meta+drag),
// the window is drawn shrunk around the cursor once its left or right edge
// goes into the outer quarter of the screen (main stays full size), reaching
// minScale at the screen edge. Quick tiling by dragging to the side
// is turned off while the effect is loaded, since it uses the same edges.
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
// Tiny parked windows (below iconBelow of their original size) act like
// icons: a left-button press on one is held back.
// Dragging it more than a few pixels moves the window (KWin's own move, so
// it grows back out of the edge zone); releasing it without dragging passes
// the press and release to the app as a click. So small parked windows can
// be dragged from anywhere and still work as widgets (buttons, scrolling).
// Not for KDE title bars (KWin handles those) or presses with modifiers.
//
// Keyboard (replaces KDE's quick tiling on Meta+arrows): Meta+Left/Right
// step the active window between left parking, left stash, left half of
// main, right half of main, right stash and right parking; a free window in
// main first snaps to the half on that side.
// Windows move horizontally and keep their vertical position. Meta+Up makes
// a window fill the screen height; Meta+Down undoes that. Keyboard moves
// animate (the drawing glides to the new place while the app resizes).
//
// Meta+drag gestures: while Meta is held during a drag, the direction and
// distance from the press decide a target, and the window snaps there (with
// a short glide) as a preview: up = Meta+Up, down = Meta+Down; left/right
// walk the ladder parking L, stash L, left half, right half, stash R,
// parking R, one step per threshold (a free window's first step is the half
// on that side; see halfMatch for "in a half"); a short diagonal = the half of main on that side, at
// full height if upward (see gestureFor). Releasing the mouse with
// Meta held commits it; releasing Meta returns to a normal drag.
//
// Selecting (Meta+Alt+arrows, KDE's own keys for this): activates the
// nearest window in that direction, judged by where windows are drawn (KDE's
// version uses the real frames, which are wrong for parked windows).
//
// Focus ring: the active window gets a thin outline in the accent color, as
// wide on screen at any scale, so it stands out also when tiny.
//
// Previews: hovering an icon-sized parked window makes it fly out beside
// its column, toward main, at 1:1 with the app's (resized) layout, so it is
// sharp and readable; its home spot stays reserved and the column doesn't
// move. The first waits previewDelay; moving along the column then switches
// at once (the old one glides back while the new one comes out). The pointer
// can move into the preview to use it; leaving both closes it after
// previewGrace (see updateHover).
//
// Declutter (Meta+double-click): on a window, it takes the half of main
// nearest to it at full height, and every other window in main goes to the
// stashes, split so both end up holding about as many (keeping their left-
// to-right order); on the desktop, everything in main goes to the sides.
// Each stash then shows all its windows at one scale, as large as fits the
// screen height, so its column lines up. The same Meta+double-click again
// undoes it (see declutter).
//
// Clips: text dragged out of an app and dropped on the desktop becomes a
// window instead of Plasma's sticky-note widget: it is saved as a file in
// ~/Clips and opened in KWrite where it was dropped, as if its window had
// been dragged there (held at its center), so it can be moved, parked and
// selected like any window (see dropToClip). Dropped in the parking band
// (the outer clipParkingBand of an edge zone, also onto parking icons), it
// becomes a parking icon in that column instead. Meta+C (a KDE global
// shortcut, changeable in System Settings) clips the text selected in the
// active window the same way, into parking on the side nearer that window
// (see clipSelection).
//
// Stacks: the windows in each stash and parking area form one column,
// centered vertically, in the order of their vertical position (see
// arrangeArea). Whenever a window arrives (keyboard or drop) or leaves
// (keyboard, dragged out, closed), the column re-forms, animated. Crowding
// (a column taller than the screen) comes later.
//
// Known gaps: touch and tablets aren't handled; in the forwarding case the
// title bar doesn't respond and the cursor shape may be wrong.

#include <core/output.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <input.h>
#include <input_event.h>
#include <main.h>
#include <options.h>
#include <pointer_input.h>
#include <scene/outlinedborderitem.h>
#include <scene/windowitem.h>
#include <utils/filedescriptor.h>
#include <wayland/abstract_data_source.h>
#include <wayland/clientconnection.h>
#include <wayland/seat.h>
#include <wayland/surface.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <KGlobalAccel>

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QPalette>
#include <QPointer>
#include <QProcess>
#include <QSocketNotifier>
#include <QStyleHints>
#include <QTimer>
#include <QTransform>

#include <algorithm>
#include <functional>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>

#include <fcntl.h>
#include <unistd.h>

using namespace KWin;

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
        connect(workspace(), &Workspace::windowActivated, this, &Glance::updateRing);
        connect(workspace(), &Workspace::windowAdded, this, &Glance::placeClip);

        auto clipAction = new QAction(this);
        clipAction->setObjectName(QStringLiteral("Glance Clip Selection"));
        clipAction->setText(QStringLiteral("Glance: Clip the Selected Text"));
        KGlobalAccel::self()->setGlobalShortcut(clipAction, QKeySequence(Qt::META | Qt::Key_C));
        connect(clipAction, &QAction::triggered, this, &Glance::clipSelection);

        m_previewOpen.setSingleShot(true);
        m_previewOpen.setInterval(previewDelay);
        connect(&m_previewOpen, &QTimer::timeout, this, [this]() {
            if (m_previewCandidate && isIcon(m_previewCandidate)) {
                openPreview(m_previewCandidate);
            }
        });
        m_previewClose.setSingleShot(true);
        m_previewClose.setInterval(previewGrace);
        connect(&m_previewClose, &QTimer::timeout, this, [this]() {
            if (Window *window = previewWindow()) {
                closePreview(window);
            }
        });
        updateRing();

        disableKdeShortcuts();

        m_savedTiling = options->electricBorderTiling();
        options->setElectricBorderTiling(false);
        // Keep it off if the settings are reloaded.
        connect(options, &Options::electricBorderTilingChanged, this, []() {
            if (options->electricBorderTiling()) {
                options->setElectricBorderTiling(false);
            }
        });

        qInfo("glance: effect loaded");
    }

    ~Glance() override
    {
        input()->uninstallInputEventFilter(&m_filter);
        removeRing();
        if (m_clip) {
            m_clip->notifier.reset();
            close(m_clip->fd);
        }
        disconnect(options, nullptr, this, nullptr);
        options->setElectricBorderTiling(m_savedTiling);
        for (const QPointer<QAction> &action : m_disabledActions) {
            if (action) {
                action->setEnabled(true);
            }
        }
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

    // Advance animations: each frame, redraw animating windows at their
    // current place; finished ones settle.
    void prePaintScreen(ScreenPrePaintData &data) override
    {
        std::vector<Window *> animating;
        for (auto &[window, parked] : m_parked) {
            if (parked.animating) {
                if (progress(parked) >= 1.0) {
                    parked.animating = false;
                }
                animating.push_back(window);
            }
        }
        for (Window *window : animating) {
            applyParked(window);
        }
        if (m_dragged && m_dragAnimating) {
            dragStep(m_dragged);
        }
        Effect::prePaintScreen(data);
    }

    void postPaintScreen() override
    {
        if (m_dragAnimating) {
            effects->addRepaintFull();
        }
        for (const auto &[window, parked] : m_parked) {
            if (parked.animating) {
                effects->addRepaintFull();
                break;
            }
        }
        Effect::postPaintScreen();
    }

    // Meta+arrows and Meta+Alt+arrows (see the header comment).
    bool onKey(KeyboardKeyEvent *event)
    {
        if (m_dragged) {
            // Meta pressed or released during a drag: switch between gesture
            // and normal drag now, once KWin has updated its modifier state.
            QTimer::singleShot(0, this, [this]() {
                if (m_dragged) {
                    dragStep(m_dragged);
                }
            });
        }
        const Qt::Key key = event->key;
        if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down) {
            return false;
        }
        if (event->modifiers == (Qt::MetaModifier | Qt::AltModifier)) {
            if (event->state != KeyboardKeyState::Released && !workspace()->moveResizeWindow()) {
                selectToward(key);
            }
            return false; // passed on, like Meta+arrows below
        }
        if (event->modifiers != Qt::MetaModifier) {
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
        // Pass the key on: KDE's shortcut system must see it, or it takes
        // releasing Meta as Meta tapped alone and opens the launcher. Its own
        // quick tiling on these keys is disabled (see disableKdeShortcuts).
        return false;
    }

    bool onMotion(PointerMotionEvent *event)
    {
        if (m_pending) {
            return pendingMotion(event);
        }
        updateHover(event->position, event->buttons);
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
        if (pressed) {
            // Where a drag that may follow started (KWin's own move anchor
            // follows the cursor, so it can't tell us).
            m_lastPress = event->position;
        }
        if (metaDoubleClick(event)) {
            return true;
        }
        if (!pressed && dropToClip(event)) {
            return true;
        }
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
        explicit Filter(Glance *effect)
            : InputEventFilter(InputFilterOrder::ButtonRebind)
            , m_effect(effect)
        {
        }
        bool keyboardKey(KeyboardKeyEvent *event) override { return m_effect->onKey(event); }
        bool pointerMotion(PointerMotionEvent *event) override { return m_effect->onMotion(event); }
        bool pointerButton(PointerButtonEvent *event) override { return m_effect->onButton(event); }
        bool pointerAxis(PointerAxisEvent *event) override { return m_effect->onAxis(event); }

    private:
        Glance *m_effect;
    };

    Filter m_filter;

    enum class Side { Left, Right };
    // Where a window is, for Meta+Left/Right. Each side has a parking area, a
    // stash and a half of main; Free is anywhere else.
    enum class Place { ParkingLeft, StashLeft, HalfLeft, HalfRight, StashRight, ParkingRight, Free };

    // --- Tuning knobs ---
    // Width of the left and right edge zones, where shrinking happens, as a
    // fraction of the screen width: main (the middle half) stays full size.
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
        // Drawn here instead of `shown` while hovered (see updateHover).
        std::optional<QRectF> preview = std::nullopt;
        // Animating from `from` to `shown` since `start`.
        bool animating = false;
        QRectF from = {};
        std::chrono::steady_clock::time_point start = {};
    };
    std::map<Window *, Parked> m_parked;

    // The window being dragged while we draw it scaled, its original size,
    // and its scale relative to that.
    QPointer<Window> m_dragged;
    QSizeF m_dragOriginal;
    qreal m_dragScale = 1.0;

    // A Meta+drag gesture's target: a place, or filling / unfilling height.
    struct Gesture
    {
        int key; // tells targets apart
        QRectF drawn; // where the window is shown meanwhile
        std::optional<Place> place = std::nullopt;
        bool fill = false;
        bool unfill = false;
    };
    // Where the dragged window was when the drag started, where it is drawn
    // now, the current gesture target, and the glide between them.
    Place m_dragStartPlace = Place::Free;
    QPointF m_lastPress;
    QPointF m_dragPress;
    QRectF m_dragStartFrame;
    qreal m_dragStartCenterY = 0;
    QRectF m_dragDisplayed;
    std::optional<Gesture> m_dragGesture;
    int m_dragModeKey = -1;
    bool m_dragAnimating = false;
    QRectF m_dragAnimFrom;
    std::chrono::steady_clock::time_point m_dragAnimStart;

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

    // Text being turned into a clip (see startClip): the text read so far,
    // where the clip goes, and for a drop (see dropToClip) the held-back
    // release.
    struct ClipRead
    {
        int fd = -1;
        std::unique_ptr<QSocketNotifier> notifier = nullptr;
        QByteArray text = {};
        QPointF position;
        std::optional<Place> place = std::nullopt;
        bool fromDrag = false;
        quint32 nativeButton = 0;
        std::chrono::microseconds timestamp = {};
    };
    std::unique_ptr<ClipRead> m_clip;
    // The KWrite started for the last clip, where its window goes, and
    // whether it goes to parking (see placeClip).
    qint64 m_clipPid = 0;
    QPointF m_clipPosition;
    std::optional<Place> m_clipPlace;

    // The last Meta+left press (for double-clicks), and whether the release
    // of a double-click's second press is to be swallowed too.
    std::optional<std::pair<QPointF, std::chrono::microseconds>> m_metaPress;
    bool m_swallowRelease = false;

    // How windows were before the last declutter, for undoing it: the
    // target (null for the desktop), the half it went to, and each window's
    // state then.
    struct Saved
    {
        QPointer<Window> window;
        std::optional<Parked> parked; // parked then, else free:
        RectF frame;
        MaximizeMode maximize = MaximizeRestore;
    };
    struct Declutter
    {
        bool desktop;
        QPointer<Window> target;
        Place half = Place::Free;
        std::vector<Saved> saved;
    };
    std::optional<Declutter> m_declutter;

    // The previewed window, the one the pointer waits on, and the timers to
    // open and close previews (see updateHover).
    QPointer<Window> m_preview;
    QPointer<Window> m_previewCandidate;
    QTimer m_previewOpen;
    QTimer m_previewClose;

    // The focus ring (see updateRing) and the window it outlines.
    OutlinedBorderItem *m_ring = nullptr;
    QPointer<Window> m_ringWindow;

    // KDE's shortcut actions we disabled, to re-enable on unload.
    std::vector<QPointer<QAction>> m_disabledActions;

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
            if (window == m_ringWindow) {
                updateRing();
            }
        });
        connect(window, &Window::fullScreenChanged, this, [this, window]() {
            if (window == workspace()->activeWindow()) {
                updateRing();
            }
        });
        connect(window, &Window::closed, this, [this, window]() {
            if (window == m_ringWindow) {
                removeRing(); // while its parent item still exists
            }
            const auto closeRanks = leaving(window);
            m_parked.erase(window);
            m_beforeFillHeight.erase(window);
            closeRanks();
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
        if (parked.restoring || parked.preview || parked.shown.width() / parked.original.width() >= iconBelow) {
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

    // Our Meta+arrows replace KDE's quick tiling on the same keys, and our
    // Meta+Alt+arrows its "Switch to Window" ones. Rather than hiding the
    // keys from KDE's shortcut system (which then opens the launcher when
    // Meta is released), disable KWin's actions for them: the shortcut still
    // matches, and a disabled action does nothing. Only while the effect is
    // loaded; nothing is saved to the user's settings.
    void disableKdeShortcuts()
    {
        for (const char *name : {"Window Quick Tile Left", "Window Quick Tile Right",
                                 "Window Quick Tile Top", "Window Quick Tile Bottom",
                                 "Switch Window Left", "Switch Window Right",
                                 "Switch Window Up", "Switch Window Down"}) {
            QAction *action = workspace()->findChild<QAction *>(QString::fromLatin1(name));
            if (!action) {
                qWarning("glance: KWin action \"%s\" not found", name);
                continue;
            }
            if (action->isEnabled()) {
                action->setEnabled(false);
                m_disabledActions.push_back(action);
            }
        }
    }


    // A window counts as being in a half of main when its horizontal
    // extent and the half's share at least this much (intersection over
    // union), so a slightly moved or resized one still does.
    static constexpr qreal halfMatch = 0.8;

    // Hover previews: wait before the first one opens, grace before one
    // closes after the pointer left, and the gap to its column.
    static constexpr std::chrono::milliseconds previewDelay{300};
    static constexpr std::chrono::milliseconds previewGrace{300};
    static constexpr qreal previewGap = 8.0;

    // Where clips are saved, relative to the home folder.
    static constexpr const char *clipsFolder = "Clips";
    // Text dropped this close to a screen edge (fraction of the edge zone's
    // width) becomes a parking icon (see placeClip).
    static constexpr qreal clipParkingBand = 0.15;
    // How long to wait for a dragging app to hand over its text.
    static constexpr std::chrono::milliseconds clipTimeout{2000};

    // Width of the focus ring on screen (logical pixels).
    static constexpr qreal ringWidth = 2.0;

    // Scale of a window in a stash when put there with the keyboard.
    static constexpr qreal stashScale = 0.5;
    // Length of keyboard moves and making-room animations.
    static constexpr std::chrono::milliseconds animationTime{180};
    // Vertical gap between windows that made room for each other.
    static constexpr qreal arrangeGap = 8.0;
    // Meta+drag gestures: distance (logical px) from the press for the first
    // step, then for each further step, and how far off an axis (as tan of
    // the angle) a drag may go and still count as that direction (30 deg).
    static constexpr qreal gestureStep1 = 150.0;
    static constexpr qreal gestureStepEach = 250.0;
    static constexpr qreal gestureCone = 0.577;
    // A diagonal drag at least this long (and not yet at gestureStep1
    // sideways) snaps to the half of main on that side.
    static constexpr qreal gestureDiagonal = 100.0;

    // The places Meta+Left/Right and gestures step along.
    static constexpr Place placeOrder[] = {Place::ParkingLeft, Place::StashLeft, Place::HalfLeft,
                                           Place::HalfRight, Place::StashRight, Place::ParkingRight};
    static int placeIndex(Place place)
    {
        return int(std::find(std::begin(placeOrder), std::end(placeOrder), place) - std::begin(placeOrder));
    }

    Place placeOf(Window *window) const
    {
        const RectF screen = window->output()->geometryF();
        auto it = m_parked.find(window);
        if (it != m_parked.end() && !it->second.restoring) {
            const Parked &parked = it->second;
            const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
            const bool tiny = parked.shown.width() / parked.original.width() < minScale + 0.02;
            if (tiny) {
                return left ? Place::ParkingLeft : Place::ParkingRight;
            }
            return left ? Place::StashLeft : Place::StashRight;
        }
        const RectF frame = window->moveResizeGeometry();
        return placeOfFrame(window, QRectF(frame.x(), frame.y(), frame.width(), frame.height()));
    }

    // For a window not parked: in a half of main (see halfMatch), or
    // free.
    Place placeOfFrame(Window *window, const QRectF &frame) const
    {
        const RectF screen = window->output()->geometryF();
        const qreal zoneWidth = screen.width() * zoneFraction;
        auto share = [&](qreal x) {
            const qreal inter = std::min(frame.right(), x + zoneWidth) - std::max(frame.left(), x);
            const qreal uni = std::max(frame.right(), x + zoneWidth) - std::min(frame.left(), x);
            return std::max(0.0, inter) / uni;
        };
        if (share(screen.x() + zoneWidth) >= halfMatch) {
            return Place::HalfLeft;
        }
        if (share(screen.x() + 2 * zoneWidth) >= halfMatch) {
            return Place::HalfRight;
        }
        return Place::Free;
    }

    // One step towards `side` along: parking L, stash L, half L, half R,
    // stash R, parking R. A free window goes to the half on that side.
    void stepSideways(Window *window, Side side)
    {
        const Place from = placeOf(window);
        Place to;
        if (from == Place::Free) {
            to = side == Side::Left ? Place::HalfLeft : Place::HalfRight;
        } else {
            const int i = placeIndex(from);
            const int j = std::clamp(i + (side == Side::Left ? -1 : 1), 0, 5);
            if (i == j) {
                return;
            }
            to = placeOrder[j];
        }
        moveTo(window, to);
    }

    // `scale`: for a stash, the scale to show it at.
    void moveTo(Window *window, Place place, qreal scale = stashScale)
    {
        // KDE's own maximized or tiled state would fight our geometry.
        if (window->maximizeMode() != MaximizeRestore) {
            window->maximize(MaximizeRestore);
        }
        if (window->quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
            window->setQuickTileModeAtCurrentPosition(QuickTileFlag::None);
        }
        m_beforeFillHeight.erase(window);

        // Where it is drawn now: the animation starts there.
        const QRectF from = currentlyDrawn(window);
        const auto closeRanks = leaving(window);

        // Its full (unparked) size, and the vertical center it keeps.
        auto it = m_parked.find(window);
        const bool parked = it != m_parked.end() && !it->second.restoring;
        const RectF current = window->moveResizeGeometry();
        const QSizeF size = parked ? it->second.original : QSizeF(current.width(), current.height());
        const qreal centerY = parked ? it->second.shown.center().y() : current.y() + current.height() / 2;
        commitPlace(window, place, size, centerY, from, scale);
        closeRanks();
    }

    // Where a window of full size `size`, centered at `centerY`, goes in
    // `place`: for the halves of main its new frame, for a stash or parking
    // area where it is drawn (a stash at `stash` scale).
    QRectF placeRect(Window *window, Place place, const QSizeF &size, qreal centerY, qreal stash = stashScale) const
    {
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const qreal zoneWidth = screen.width() * zoneFraction;
        auto topFor = [&](qreal height) {
            return std::clamp(centerY - height / 2, area.y(), std::max(area.y(), area.y() + area.height() - height));
        };
        switch (place) {
        case Place::HalfLeft:
        case Place::HalfRight: {
            const qreal height = std::min(size.height(), area.height());
            const qreal x = screen.x() + (place == Place::HalfLeft ? zoneWidth : 2 * zoneWidth);
            return QRectF(QPointF(x, topFor(height)), QSizeF(zoneWidth, height));
        }
        case Place::StashLeft:
        case Place::StashRight:
        case Place::ParkingLeft:
        case Place::ParkingRight: {
            const qreal scale = place == Place::StashLeft || place == Place::StashRight ? stash : minScale;
            const QSizeF drawn = size * scale;
            // Where the drag rule gives this scale: the outer edge this far in.
            const qreal depth = (scale - minScale) / (1.0 - minScale) * zoneWidth;
            const bool left = place == Place::StashLeft || place == Place::ParkingLeft;
            const qreal x = left ? screen.x() + depth : screen.x() + screen.width() - depth - drawn.width();
            return QRectF(QPointF(x, topFor(drawn.height())), drawn);
        }
        case Place::Free:
            break;
        }
        return QRectF();
    }

    // Put a window in `place` (see placeRect), gliding from `from`.
    void commitPlace(Window *window, Place place, const QSizeF &size, qreal centerY, const QRectF &from,
                     qreal stash = stashScale)
    {
        const QRectF rect = placeRect(window, place, size, centerY, stash);
        switch (place) {
        case Place::HalfLeft:
        case Place::HalfRight:
            resizeAnimated(window, RectF(rect.x(), rect.y(), rect.width(), rect.height()), from);
            break;
        case Place::StashLeft:
        case Place::StashRight:
        case Place::ParkingLeft:
        case Place::ParkingRight:
            park(window, rect, size);
            animate(window, from);
            arrange(window);
            break;
        case Place::Free:
            break;
        }
    }

    // Really resize (and move) a window to `target`, drawing it gliding
    // there from `from`: until the app has its new size and the animation is
    // over, it is drawn scaled (the "restoring" state).
    void resizeAnimated(Window *window, const RectF &target, const QRectF &from)
    {
        m_parked[window] = Parked{.shown = QRectF(target.x(), target.y(), target.width(), target.height()),
                                  .original = QSizeF(target.width(), target.height()),
                                  .restoring = true};
        window->moveResize(target);
        animate(window, from);
    }

    // Meta+Up: fill the screen height, keeping width and x.
    void fillHeight(Window *window)
    {
        if (isParkedNotRestoring(window)) {
            return;
        }
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const RectF current = window->moveResizeGeometry();
        if (!m_beforeFillHeight.contains(window)) {
            m_beforeFillHeight[window] = {current.y(), current.height()};
        }
        resizeAnimated(window, RectF(current.x(), area.y(), current.width(), area.height()), currentlyDrawn(window));
    }

    // Meta+Down: back to the height before Meta+Up.
    void undoFillHeight(Window *window)
    {
        auto it = m_beforeFillHeight.find(window);
        if (it == m_beforeFillHeight.end() || isParkedNotRestoring(window)) {
            return;
        }
        const RectF current = window->moveResizeGeometry();
        const RectF target(current.x(), it->second.first, current.width(), it->second.second);
        m_beforeFillHeight.erase(it);
        resizeAnimated(window, target, currentlyDrawn(window));
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
    // cursor. Draw it scaled around the cursor by the edge rule, or, while
    // Meta is held and the drag matches a gesture, at the gesture's target;
    // switching between the two glides. A parked window being dragged stops
    // being parked: its frame was re-anchored around the cursor when it was
    // grabbed, so the drag continues from where it is drawn.
    void dragStep(Window *window)
    {
        if (!window->isInteractiveMove() || !window->windowItem()) {
            return;
        }
        const RectF frame = window->frameGeometry();
        const QPointF cursor = input()->pointer()->pos();
        if (m_dragged != window) {
            // Start of a drag. The scale is relative to the original size,
            // which a parked (resized) window remembers.
            auto it = m_parked.find(window);
            const bool parked = it != m_parked.end() && !it->second.restoring;
            m_dragOriginal = it != m_parked.end() ? it->second.original : QSizeF(frame.width(), frame.height());
            m_dragPress = m_lastPress;
            const QPointF moved = cursor - m_dragPress;
            m_dragStartFrame = QRectF(frame.x() - moved.x(), frame.y() - moved.y(), frame.width(), frame.height());
            m_dragStartPlace = parked ? placeOf(window) : placeOfFrame(window, m_dragStartFrame);
            m_dragStartCenterY = parked ? it->second.shown.center().y() : m_dragStartFrame.center().y();
            m_dragDisplayed = currentlyDrawn(window);
            m_dragGesture.reset();
            m_dragModeKey = -1;
            m_dragAnimating = false;
            const auto closeRanks = leaving(window);
            m_parked.erase(window);
            m_dragged = window;
            closeRanks();
        }

        std::optional<Gesture> gesture;
        if (input()->keyboardModifiers() & Qt::MetaModifier) {
            gesture = gestureFor(window, cursor - m_dragPress);
        }
        const QRectF want = gesture ? gesture->drawn : followRect(window, frame, cursor);
        const int key = gesture ? gesture->key : -1;
        if (key != m_dragModeKey) {
            m_dragModeKey = key;
            m_dragAnimFrom = m_dragDisplayed;
            m_dragAnimStart = std::chrono::steady_clock::now();
            m_dragAnimating = true;
        }
        QRectF shown = want;
        if (m_dragAnimating) {
            const auto elapsed = std::chrono::steady_clock::now() - m_dragAnimStart;
            const qreal t = std::clamp(std::chrono::duration<qreal>(elapsed) / animationTime, 0.0, 1.0);
            if (t >= 1.0) {
                m_dragAnimating = false;
            } else {
                shown = lerpRect(m_dragAnimFrom, want, 1.0 - std::pow(1.0 - t, 3));
            }
        }
        m_dragDisplayed = shown;
        m_dragGesture = gesture;

        // The item's coordinates start at the frame's top-left corner.
        QTransform transform;
        transform.translate(shown.x() - frame.x(), shown.y() - frame.y());
        transform.scale(shown.width() / frame.width(), shown.height() / frame.height());
        setDrawTransform(window, transform);
    }

    // Where the edge rule draws the dragged window: scaled around the cursor.
    QRectF followRect(Window *window, const RectF &frame, const QPointF &cursor)
    {
        const RectF screen = window->moveResizeOutput()->geometryF();
        const qreal left = frame.x();
        const qreal right = frame.x() + frame.width();
        // From the current frame size to the original size.
        const qreal grow = m_dragOriginal.width() / frame.width();

        // Full size while the window stays within main (the middle half);
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
        return QRectF(drawnLeft + shift, cursor.y() + (frame.y() - cursor.y()) * scale,
                      frame.width() * scale, frame.height() * scale);
    }

    // The gesture target for a Meta+drag that has moved `delta` from the
    // press, if any. Left/right: from main (free or a half), the stash on that
    // side, then (further) its parking area; from a stash or a parking area,
    // one or two steps along placeOrder, not past the half of main on that
    // side. Up/down (windows in main): fill height /
    // undo it.
    std::optional<Gesture> gestureFor(Window *window, const QPointF &delta) const
    {
        const qreal ax = std::abs(delta.x());
        const qreal ay = std::abs(delta.y());
        const bool inMain = m_dragStartPlace == Place::Free || m_dragStartPlace == Place::HalfLeft
            || m_dragStartPlace == Place::HalfRight;
        const bool diagonal = ay > ax * gestureCone && ax > ay * gestureCone;
        if (inMain && diagonal && ax < gestureStep1 && std::hypot(ax, ay) >= gestureDiagonal) {
            // Half of main on that side; upward also fills the height.
            const Place half = delta.x() < 0 ? Place::HalfLeft : Place::HalfRight;
            QRectF rect = placeRect(window, half, m_dragOriginal, m_dragStartCenterY);
            const bool up = delta.y() < 0;
            if (up) {
                const RectF area = workspace()->clientArea(MaximizeArea, window);
                rect.setTop(area.y());
                rect.setHeight(area.height());
            }
            return Gesture{.key = int(half) + (up ? 200 : 0), .drawn = rect, .place = half, .fill = up};
        }
        // Far enough sideways, stash and parking win over up/down.
        if (ay <= ax * gestureCone || (inMain && ax >= gestureStep1)) {
            if (ax < gestureStep1) {
                return std::nullopt;
            }
            const int steps = 1 + int((ax - gestureStep1) / gestureStepEach);
            const bool left = delta.x() < 0;
            // A free window's first step is the half on that side.
            int j;
            if (m_dragStartPlace == Place::Free) {
                j = left ? placeIndex(Place::HalfLeft) + 1 - steps : placeIndex(Place::HalfRight) - 1 + steps;
            } else {
                j = placeIndex(m_dragStartPlace) + (left ? -steps : steps);
            }
            j = std::clamp(j, 0, 5);
            if (placeOrder[j] == m_dragStartPlace) {
                return std::nullopt;
            }
            const Place to = placeOrder[j];
            return Gesture{.key = int(to), .drawn = placeRect(window, to, m_dragOriginal, m_dragStartCenterY), .place = to};
        }
        if (ax <= ay * gestureCone && inMain && ay >= gestureStep1) {
            const RectF area = workspace()->clientArea(MaximizeArea, window);
            const QRectF &start = m_dragStartFrame;
            if (delta.y() < 0) {
                return Gesture{.key = 100, .drawn = QRectF(start.x(), area.y(), start.width(), area.height()), .fill = true};
            }
            auto it = m_beforeFillHeight.find(window);
            if (it != m_beforeFillHeight.end()) {
                return Gesture{.key = 101, .drawn = QRectF(start.x(), it->second.first, start.width(), it->second.second), .unfill = true};
            }
        }
        return std::nullopt;
    }

    // Released with a gesture target: go there, gliding from where it is
    // shown.
    void commitGesture(Window *window, const Gesture &gesture, const QRectF &from)
    {
        const QRectF &start = m_dragStartFrame;
        if (gesture.place && gesture.fill) {
            // A half of main at full height.
            m_beforeFillHeight[window] = {start.y(), start.height()};
            const QRectF &r = gesture.drawn;
            resizeAnimated(window, RectF(r.x(), r.y(), r.width(), r.height()), from);
        } else if (gesture.place) {
            commitPlace(window, *gesture.place, m_dragOriginal, m_dragStartCenterY, from);
        } else if (gesture.fill) {
            const RectF area = workspace()->clientArea(MaximizeArea, window);
            if (!m_beforeFillHeight.contains(window)) {
                m_beforeFillHeight[window] = {start.y(), start.height()};
            }
            resizeAnimated(window, RectF(start.x(), area.y(), start.width(), area.height()), from);
        } else if (gesture.unfill) {
            auto it = m_beforeFillHeight.find(window);
            if (it != m_beforeFillHeight.end()) {
                const RectF target(start.x(), it->second.first, start.width(), it->second.second);
                m_beforeFillHeight.erase(it);
                resizeAnimated(window, target, from);
            }
        }
    }

    // Dropped: park it where it is drawn, or restore full size.
    void dragFinished(Window *window)
    {
        if (window != m_dragged) {
            return;
        }
        m_dragged = nullptr;
        m_dragAnimating = false;
        const std::optional<Gesture> gesture = m_dragGesture;
        m_dragGesture.reset();
        if (!window->windowItem()) {
            return;
        }
        if (gesture) {
            commitGesture(window, *gesture, m_dragDisplayed);
            return;
        }

        const RectF frame = window->frameGeometry();
        const QRectF drawn = window->windowItem()->transform()
                                 .mapRect(QRectF(0, 0, frame.width(), frame.height()))
                                 .translated(frame.topLeft());

        if (m_dragScale < parkBelow) {
            park(window, drawn, m_dragOriginal);
            arrange(window);
        } else if (m_dragOriginal != QSizeF(frame.width(), frame.height())) {
            // Back to the original size, keeping the grabbed spot under the
            // cursor: the drawing grows around it until the app has resized.
            const QPointF cursor = input()->pointer()->pos();
            const qreal grow = m_dragOriginal.width() / frame.width();
            const QRectF target(cursor - (cursor - frame.topLeft()) * grow, m_dragOriginal);
            m_parked[window] = Parked{.shown = target, .original = m_dragOriginal, .restoring = true};
            qInfo("glance: %s: restore to %.0fx%.0f", qPrintable(window->caption()),
                  m_dragOriginal.width(), m_dragOriginal.height());
            window->moveResize(RectF(target.topLeft(), m_dragOriginal));
            applyParked(window);
        } else {
            setDrawTransform(window, QTransform());
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
            qInfo("glance: %s: original %.0fx%.0f, app minimum %.0fx%.0f, shown %.0fx%.0f -> resize to %.0fx%.0f",
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

    bool isParkedNotRestoring(Window *window) const
    {
        auto it = m_parked.find(window);
        return it != m_parked.end() && !it->second.restoring;
    }

    // --- Animation ---

    static qreal progress(const Parked &parked)
    {
        const auto elapsed = std::chrono::steady_clock::now() - parked.start;
        return std::clamp(std::chrono::duration<qreal>(elapsed) / animationTime, 0.0, 1.0);
    }

    // Where a managed window's frame is to be drawn right now: `shown`, or
    // on the way there (ease-out).
    static QRectF displayRect(const Parked &parked)
    {
        const QRectF &target = parked.preview ? *parked.preview : parked.shown;
        if (!parked.animating) {
            return target;
        }
        const qreal t = progress(parked);
        return lerpRect(parked.from, target, 1.0 - std::pow(1.0 - t, 3));
    }

    static QRectF lerpRect(const QRectF &a, const QRectF &b, qreal e)
    {
        return QRectF(a.x() + (b.x() - a.x()) * e, a.y() + (b.y() - a.y()) * e,
                      a.width() + (b.width() - a.width()) * e, a.height() + (b.height() - a.height()) * e);
    }

    // Where a window is drawn now (managed or not).
    QRectF currentlyDrawn(Window *window) const
    {
        if (isParked(window)) {
            return drawnRect(window);
        }
        const RectF frame = window->frameGeometry();
        return QRectF(frame.x(), frame.y(), frame.width(), frame.height());
    }

    // Start animating a managed window from `from` to its `shown` place.
    void animate(Window *window, const QRectF &from)
    {
        auto it = m_parked.find(window);
        if (it == m_parked.end()) {
            return;
        }
        it->second.from = from;
        it->second.start = std::chrono::steady_clock::now();
        it->second.animating = true;
        applyParked(window);
        effects->addRepaintFull();
    }

    // --- Making room ---

    // Stash or parking area, left or right, of a parked window.
    int areaOf(Window *window) const
    {
        const Parked &parked = m_parked.at(window);
        const RectF screen = window->output()->geometryF();
        const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
        const bool tiny = parked.shown.width() / parked.original.width() < minScale + 0.02;
        return (left ? 0 : 2) + (tiny ? 0 : 1);
    }

    // Re-form the column of windows in one area (see areaOf) on `output`:
    // stacked with arrangeGap between them, centered vertically in the
    // usable screen area, ordered by vertical center (an `arriving` window
    // that lands on another goes below it). Moved windows animate.
    void arrangeArea(int area, LogicalOutput *output, Window *arriving)
    {
        const RectF bounds = workspace()->clientArea(MaximizeArea, output);
        struct Item
        {
            Window *window;
            qreal key;
        };
        std::vector<Item> items;
        qreal total = 0;
        for (const auto &[window, parked] : m_parked) {
            if (parked.restoring || window->output() != output || areaOf(window) != area) {
                continue;
            }
            qreal key = parked.shown.center().y();
            if (window == arriving) {
                key += parked.shown.height() / 2;
            }
            items.push_back(Item{window, key});
            total += parked.shown.height() + (items.size() > 1 ? arrangeGap : 0);
        }
        std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            return a.key < b.key;
        });

        qreal top = std::max(bounds.y(), bounds.y() + (bounds.height() - total) / 2);
        for (const Item &item : items) {
            Parked &parked = m_parked.at(item.window);
            if (std::abs(parked.shown.y() - top) > 0.5) {
                const QRectF from = displayRect(parked);
                parked.shown.moveTop(top);
                animate(item.window, from);
            }
            top += parked.shown.height() + arrangeGap;
        }
    }

    // `window` has just arrived in a stash or parking area.
    void arrange(Window *window)
    {
        if (isParkedNotRestoring(window)) {
            arrangeArea(areaOf(window), window->output(), window);
        }
    }

    // Before a parked window leaves its area: returns a function that, once
    // it has left, re-forms the area it left.
    std::function<void()> leaving(Window *window)
    {
        if (!isParkedNotRestoring(window)) {
            return [] {};
        }
        const int area = areaOf(window);
        QPointer<LogicalOutput> output = window->output();
        return [this, area, output] {
            if (output) {
                arrangeArea(area, output, nullptr);
            }
        };
    }

    // --- Declutter ---

    // Meta+double-click (left button). The first click is left to KWin (a
    // Meta+press starts a move, which a release without motion ends); the
    // second press and its release are taken. Returns whether the event was.
    bool metaDoubleClick(PointerButtonEvent *event)
    {
        if (event->state == PointerButtonState::Released) {
            if (m_swallowRelease && event->button == Qt::LeftButton) {
                m_swallowRelease = false;
                return true;
            }
            return false;
        }
        if (event->button != Qt::LeftButton || event->modifiers != Qt::MetaModifier
            || event->buttons != Qt::LeftButton) {
            m_metaPress.reset();
            return false;
        }
        const std::chrono::milliseconds interval{QGuiApplication::styleHints()->mouseDoubleClickInterval()};
        if (m_metaPress && event->timestamp - m_metaPress->second <= interval
            && std::hypot(event->position.x() - m_metaPress->first.x(), event->position.y() - m_metaPress->first.y()) <= dragThreshold
            && !workspace()->moveResizeWindow()) {
            m_metaPress.reset();
            Window *under = pick(event->position);
            if (under && !under->isDesktop() && !manageable(under)) {
                return false; // a panel or the like: not ours
            }
            m_swallowRelease = true;
            declutter(under && !under->isDesktop() ? under : nullptr, event->position);
            return true;
        }
        m_metaPress = std::make_pair(event->position, event->timestamp);
        return false;
    }

    // A window declutter (and the keyboard moves) may put somewhere else.
    bool manageable(Window *window) const
    {
        return !window->isDeleted() && window->isNormalWindow() && !window->isFullScreen() && window->isMovable()
            && window->isResizable() && !window->isMinimized() && window->isShown() && !window->skipSwitcher()
            && window->isOnCurrentDesktop() && window->isOnCurrentActivity() && window->windowItem();
    }

    // `target` (or, if null, the desktop at `pos`) was Meta+double-clicked:
    // undo the last declutter if it was for the same target and the target
    // is still where it put it; else declutter.
    void declutter(Window *target, const QPointF &pos)
    {
        const bool again = m_declutter
            && (target ? m_declutter->target == target && placeOf(target) == m_declutter->half : m_declutter->desktop);
        if (again) {
            undoDeclutter();
            return;
        }
        LogicalOutput *output = target ? target->output() : workspace()->outputAt(pos);
        if (!output) {
            return;
        }
        const RectF screen = output->geometryF();
        const qreal middle = screen.x() + screen.width() / 2;

        Declutter saved{.desktop = !target, .target = target, .half = Place::Free, .saved = {}};
        std::vector<Window *> movers; // from main to a stash
        std::vector<Window *> stashed[2]; // already in the left / right stash
        for (Window *window : workspace()->stackingOrder()) {
            if (!manageable(window) || window->output() != output) {
                continue;
            }
            auto it = m_parked.find(window);
            const bool parked = it != m_parked.end() && !it->second.restoring;
            saved.saved.push_back(Saved{.window = window,
                                        .parked = parked ? std::optional<Parked>(it->second) : std::nullopt,
                                        .frame = window->moveResizeGeometry(),
                                        .maximize = window->maximizeMode()});
            if (window == target) {
                continue;
            }
            if (!parked) {
                movers.push_back(window);
            } else if (const int area = areaOf(window); area == 1 || area == 3) {
                stashed[area == 1 ? 0 : 1].push_back(window);
            }
        }

        // Balance the stashes: the leftmost `toLeft` movers go left, the rest
        // right, so both end up with about as many windows.
        std::sort(movers.begin(), movers.end(), [this](Window *a, Window *b) {
            return currentlyDrawn(a).center().x() < currentlyDrawn(b).center().x();
        });
        const int count = int(movers.size());
        const int toLeft = std::clamp(int(std::lround((count + int(stashed[1].size()) - int(stashed[0].size())) / 2.0)), 0, count);
        stashed[0].insert(stashed[0].end(), movers.begin(), movers.begin() + toLeft);
        stashed[1].insert(stashed[1].end(), movers.begin() + toLeft, movers.end());

        if (target) {
            saved.half = currentlyDrawn(target).center().x() < middle ? Place::HalfLeft : Place::HalfRight;
            fillHalf(target, saved.half);
        }
        // Each stash at one scale, so its column lines up.
        for (int side = 0; side < 2; ++side) {
            if (movers.empty() || stashed[side].empty()) {
                continue;
            }
            const Place stash = side == 0 ? Place::StashLeft : Place::StashRight;
            const qreal scale = fittingScale(output, stashed[side]);
            for (Window *window : stashed[side]) {
                moveTo(window, stash, scale);
            }
        }
        if (target) {
            workspace()->activateWindow(target);
        }
        m_declutter = std::move(saved);
    }

    // `window` into `half` of main at the full usable height, gliding.
    void fillHalf(Window *window, Place half)
    {
        if (window->maximizeMode() != MaximizeRestore) {
            window->maximize(MaximizeRestore);
        }
        if (window->quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
            window->setQuickTileModeAtCurrentPosition(QuickTileFlag::None);
        }
        m_beforeFillHeight.erase(window);
        const QRectF from = currentlyDrawn(window);
        const auto closeRanks = leaving(window);
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const qreal zoneWidth = screen.width() * zoneFraction;
        const qreal x = screen.x() + (half == Place::HalfLeft ? zoneWidth : 2 * zoneWidth);
        resizeAnimated(window, RectF(x, area.y(), zoneWidth, area.height()), from);
        closeRanks();
    }

    // The scale (at most stashScale, a little above minScale so it stays a
    // stash) at which `windows`, at their full sizes, fit in one column.
    qreal fittingScale(LogicalOutput *output, const std::vector<Window *> &windows) const
    {
        const RectF area = workspace()->clientArea(MaximizeArea, output);
        qreal heights = 0;
        for (Window *window : windows) {
            auto it = m_parked.find(window);
            heights += it != m_parked.end() && !it->second.restoring ? it->second.original.height()
                                                                     : window->moveResizeGeometry().height();
        }
        const qreal room = area.height() - arrangeGap * (int(windows.size()) - 1);
        return std::clamp(room / heights, minScale + 0.03, stashScale);
    }

    // Everything back as it was before the last declutter (windows closed
    // since are skipped).
    void undoDeclutter()
    {
        const Declutter declutter = std::move(*m_declutter);
        m_declutter.reset();
        for (const Saved &saved : declutter.saved) {
            Window *window = saved.window;
            if (!window || window->isDeleted() || !window->windowItem()) {
                continue;
            }
            const QRectF from = currentlyDrawn(window);
            if (saved.parked) {
                park(window, saved.parked->shown, saved.parked->original);
                animate(window, from);
            } else if (saved.maximize != MaximizeRestore) {
                m_parked.erase(window);
                setDrawTransform(window, QTransform());
                window->maximize(saved.maximize);
            } else if (isParked(window) || window->moveResizeGeometry() != saved.frame) {
                resizeAnimated(window, saved.frame, from);
            }
        }
        if (declutter.target) {
            workspace()->activateWindow(declutter.target);
        }
    }

    // --- Hover previews ---

    // A parked window shown small enough to act like an icon (see iconBelow).
    bool isIcon(Window *window) const
    {
        auto it = m_parked.find(window);
        return it != m_parked.end() && !it->second.restoring
            && it->second.shown.width() / it->second.original.width() < iconBelow;
    }

    // The previewed window, if it still is one.
    Window *previewWindow()
    {
        if (m_preview && !(isParked(m_preview) && m_parked.at(m_preview).preview)) {
            m_preview = nullptr;
        }
        return m_preview;
    }

    // The icon whose home spot in its column is at `pos` (the spot counts
    // also while that window is out as a preview), half the gap around it
    // included so moving along the column never falls between two.
    Window *iconAt(const QPointF &pos) const
    {
        for (const auto &[window, parked] : m_parked) {
            if (isIcon(window) && !window->isMinimized() && window->isOnCurrentDesktop()
                && parked.shown.adjusted(0, -arrangeGap / 2, 0, arrangeGap / 2).contains(pos)) {
                return window;
            }
        }
        return nullptr;
    }

    // On every pointer motion: the icon under the pointer comes out after
    // previewDelay, or at once if another is out already (that one glides
    // back at the same time). Over the preview itself it stays out; anywhere
    // else it goes back after previewGrace. Nothing changes while a button is
    // held, a window is moved, or something is dragged.
    void updateHover(const QPointF &pos, Qt::MouseButtons buttons)
    {
        if (buttons != Qt::NoButton || workspace()->moveResizeWindow() || waylandServer()->seat()->isDrag()) {
            m_previewOpen.stop();
            return;
        }
        Window *preview = previewWindow();
        if (preview && drawnRect(preview).contains(pos)) {
            m_previewOpen.stop();
            m_previewClose.stop();
            return;
        }
        Window *icon = iconAt(pos);
        if (!icon) {
            m_previewOpen.stop();
            m_previewCandidate = nullptr;
            if (preview && !m_previewClose.isActive()) {
                m_previewClose.start();
            }
            return;
        }
        m_previewClose.stop();
        if (icon == preview) {
            m_previewOpen.stop();
        } else if (preview) {
            m_previewOpen.stop();
            openPreview(icon);
        } else if (icon != m_previewCandidate || !m_previewOpen.isActive()) {
            m_previewCandidate = icon;
            m_previewOpen.start();
        }
    }

    // Beside its column, toward main: 1:1 with the app's current layout,
    // scaled down only to fit the screen, vertically centered on its spot.
    QRectF previewRect(Window *window) const
    {
        const Parked &parked = m_parked.at(window);
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const RectF frame = window->frameGeometry();
        const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
        const qreal room = left ? screen.x() + screen.width() - parked.shown.right() - previewGap
                                : parked.shown.left() - previewGap - screen.x();
        const qreal scale = std::min({1.0, area.height() / frame.height(), room / frame.width()});
        const QSizeF size = QSizeF(frame.width(), frame.height()) * scale;
        const qreal x = left ? parked.shown.right() + previewGap : parked.shown.left() - previewGap - size.width();
        const qreal y = std::clamp(parked.shown.center().y() - size.height() / 2, area.y(),
                                   std::max(area.y(), area.y() + area.height() - size.height()));
        return QRectF(QPointF(x, y), size);
    }

    void openPreview(Window *window)
    {
        if (Window *old = previewWindow(); old && old != window) {
            closePreview(old);
        }
        m_previewCandidate = nullptr;
        const QRectF from = currentlyDrawn(window);
        m_parked.at(window).preview = previewRect(window);
        m_preview = window;
        workspace()->raiseWindow(window);
        animate(window, from);
    }

    void closePreview(Window *window)
    {
        const QRectF from = currentlyDrawn(window);
        m_parked.at(window).preview.reset();
        if (window == m_preview) {
            m_preview = nullptr;
        }
        animate(window, from);
    }

    // --- Clips: text dropped on the desktop ---

    // A text drag released over the desktop: rather than letting Plasma
    // make a sticky-note widget of it, ask the dragging app for the text and
    // hold the release back. Once the text is in (finishClip), the drag is
    // cancelled, so nothing is dropped anywhere, and the release passed on.
    // Returns whether the release was taken. Dragged files and links (they
    // come with text/uri-list) are left to Plasma.
    bool dropToClip(PointerButtonEvent *event)
    {
        auto seat = waylandServer()->seat();
        if (m_clip || event->button != Qt::LeftButton || !seat->isDragPointer() || !seat->dragSource()) {
            return false;
        }
        Window *under = pick(event->position);
        if (under && !under->isDesktop() && !(isIcon(under) && parkingSide(event->position))) {
            return false;
        }
        const QString mimeType = clipMimeType(seat->dragSource()->mimeTypes());
        if (mimeType.isEmpty()) {
            return false;
        }
        return startClip(seat->dragSource(), mimeType,
                         ClipRead{.position = event->position,
                                  .place = parkingSide(event->position),
                                  .fromDrag = true,
                                  .nativeButton = event->nativeButton,
                                  .timestamp = event->timestamp});
    }

    // Meta+C: clip the text selected in the active window (the primary
    // selection, if that window's app owns it: the primary selection
    // outlives the highlight and may belong to another app) into parking on
    // the side nearer the window.
    void clipSelection()
    {
        Window *window = workspace()->activeWindow();
        AbstractDataSource *source = waylandServer()->seat()->primarySelection();
        if (m_clip || !window || !window->surface() || !source) {
            return;
        }
        if (source->client() != window->surface()->client()->client()) {
            qInfo("glance: clip: no text selected in %s", qPrintable(window->caption()));
            return;
        }
        const QString mimeType = clipMimeType(source->mimeTypes());
        if (mimeType.isEmpty()) {
            return;
        }
        const QRectF drawn = currentlyDrawn(window);
        const RectF screen = window->output()->geometryF();
        const bool left = drawn.center().x() < screen.x() + screen.width() / 2;
        startClip(source, mimeType,
                  ClipRead{.position = drawn.center(), .place = left ? Place::ParkingLeft : Place::ParkingRight});
    }

    // Plain text offered as one of `types`, or empty. Files and links (they
    // come with text/uri-list) don't count.
    static QString clipMimeType(const QStringList &types)
    {
        if (types.contains(QStringLiteral("text/uri-list"))) {
            return {};
        }
        for (const char *type : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"}) {
            if (types.contains(QLatin1String(type))) {
                return QLatin1String(type);
            }
        }
        return {};
    }

    // Ask `source` for its text; it arrives in readClip, and finishClip
    // makes the clip. Returns whether it started.
    bool startClip(AbstractDataSource *source, const QString &mimeType, ClipRead clip)
    {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
            return false;
        }
        // The app writes into fds[1] (our copy is closed once sent) until
        // it closes it; we read fds[0] as data arrives.
        source->requestData(mimeType, FileDescriptor(fds[1]));
        clip.fd = fds[0];
        clip.notifier = std::make_unique<QSocketNotifier>(fds[0], QSocketNotifier::Read);
        m_clip = std::make_unique<ClipRead>(std::move(clip));
        connect(m_clip->notifier.get(), &QSocketNotifier::activated, this, &Glance::readClip);
        QTimer::singleShot(clipTimeout, this, [this, fd = fds[0]]() {
            if (m_clip && m_clip->fd == fd) {
                qWarning("glance: clip: the app took too long to hand over the text");
                finishClip();
            }
        });
        return true;
    }

    void readClip()
    {
        char buffer[4096];
        while (m_clip) {
            const ssize_t n = read(m_clip->fd, buffer, sizeof(buffer));
            if (n > 0) {
                m_clip->text.append(buffer, n);
            } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
                return; // more to come
            } else {
                finishClip(); // end of data, or an error
            }
        }
    }

    // All text read (or given up): for a drop, end the drag and pass the
    // release on; save and open the clip.
    void finishClip()
    {
        const std::unique_ptr<ClipRead> clip = std::move(m_clip);
        // We may be inside the notifier's own signal: delete it later.
        clip->notifier->setEnabled(false);
        clip->notifier.release()->deleteLater();
        close(clip->fd);

        if (clip->fromDrag) {
            auto seat = waylandServer()->seat();
            seat->cancelDrag();
            seat->setTimestamp(clip->timestamp);
            seat->notifyPointerButton(clip->nativeButton, PointerButtonState::Released);
            seat->notifyPointerFrame();
        }

        if (clip->text.trimmed().isEmpty()) {
            qWarning("glance: clip: no text received");
            return;
        }
        const QDir dir(QDir::home().filePath(QLatin1String(clipsFolder)));
        if (!dir.mkpath(QStringLiteral("."))) {
            qWarning("glance: clip: can't create %s", qPrintable(dir.path()));
            return;
        }
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
        QString path = dir.filePath(stamp + QStringLiteral(".txt"));
        for (int i = 2; QFile::exists(path); ++i) {
            path = dir.filePath(QStringLiteral("%1 (%2).txt").arg(stamp).arg(i));
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(clip->text) != clip->text.size()) {
            qWarning("glance: clip: can't write %s", qPrintable(path));
            return;
        }
        file.close();

        // KWin's own environment for the apps it starts, without the plugin
        // path that loads this effect from the build folder. Started in its
        // own systemd scope in app.slice, like apps Plasma starts: otherwise
        // it would belong to KWin's service, be stopped in an odd order at
        // logout and die with KWin. systemd-run --scope execs KWrite in its
        // own process, so the pid is KWrite's (see placeClip).
        QProcessEnvironment env = kwinApp()->processStartupEnvironment();
        env.remove(QStringLiteral("QT_PLUGIN_PATH"));
        const QString unit = QStringLiteral("app-org.kde.kwrite-glance-%1.scope").arg(QDateTime::currentMSecsSinceEpoch());
        QProcess process;
        process.setProgram(QStringLiteral("systemd-run"));
        process.setArguments({QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--slice=app.slice"),
                              QStringLiteral("--unit=") + unit, QStringLiteral("--collect"), QStringLiteral("--quiet"),
                              QStringLiteral("--"), QStringLiteral("kwrite"), path});
        process.setProcessEnvironment(env);
        qint64 pid = 0;
        if (!process.startDetached(&pid)) {
            qWarning("glance: clip: can't start kwrite");
            return;
        }
        m_clipPid = pid;
        m_clipPosition = clip->position;
        m_clipPlace = clip->place;
        qInfo("glance: clip: %lld bytes -> %s", qlonglong(clip->text.size()), qPrintable(path));
    }

    // ParkingLeft/Right if `pos` is in that side's parking band (see
    // clipParkingBand).
    std::optional<Place> parkingSide(const QPointF &pos) const
    {
        LogicalOutput *output = workspace()->outputAt(pos);
        if (!output) {
            return std::nullopt;
        }
        const RectF screen = output->geometryF();
        const qreal band = screen.width() * zoneFraction * clipParkingBand;
        if (pos.x() - screen.x() < band) {
            return Place::ParkingLeft;
        }
        if (screen.x() + screen.width() - pos.x() < band) {
            return Place::ParkingRight;
        }
        return std::nullopt;
    }

    // The clip's KWrite window appeared: put it where the text was dropped,
    // as if it had been dragged there held at its center and dropped: full
    // size in main, shrunk by the edge rule (see edgeScale) and parked if an
    // edge went into an edge zone.
    void placeClip(Window *window)
    {
        if (!m_clipPid || window->pid() != m_clipPid || !window->isNormalWindow() || !window->windowItem()) {
            return;
        }
        m_clipPid = 0;
        const QPointF pos = m_clipPosition;
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const RectF frame = window->moveResizeGeometry();
        const QSizeF size(frame.width(), frame.height());
        if (m_clipPlace) {
            // Dropped in the parking band: a parking icon in that column.
            commitPlace(window, *m_clipPlace, size, pos.y(), currentlyDrawn(window));
            workspace()->activateWindow(window);
            return;
        }
        const qreal zoneWidth = screen.width() * zoneFraction;
        const qreal scale = std::max(minScale, std::min({1.0,
                                                         edgeScale(pos.x() - screen.x(), size.width() / 2, zoneWidth),
                                                         edgeScale(screen.x() + screen.width() - pos.x(), size.width() / 2, zoneWidth)}));
        const QSizeF drawn = size * scale;
        const qreal left = pos.x() - drawn.width() / 2;
        const qreal x = left + shiftOntoScreen(left, drawn.width(), screen);
        const qreal y = std::clamp(pos.y() - drawn.height() / 2, area.y(), std::max(area.y(), area.y() + area.height() - drawn.height()));
        if (scale < parkBelow) {
            const QRectF from = currentlyDrawn(window);
            park(window, QRectF(QPointF(x, y), drawn), size);
            animate(window, from);
            arrange(window);
        } else {
            window->move(QPointF(x, y));
        }
        workspace()->activateWindow(window);
    }

    // --- Selecting and the focus ring ---

    // Meta+Alt+arrow: activate the nearest window in that direction, by
    // where windows are drawn (centers), scored like KWin's own
    // Workspace::switchWindow: distance along the arrow, plus how far off
    // to the side, plus a penalty for being far off to the side but close.
    void selectToward(Qt::Key key)
    {
        Window *active = workspace()->activeWindow();
        const QPointF from = active ? currentlyDrawn(active).center() : input()->pointer()->pos();
        Window *best = nullptr;
        qreal bestScore = 0;
        for (Window *window : workspace()->stackingOrder()) {
            // Only windows one can see (not e.g. KDE's hidden Xwayland Video
            // Bridge, which then can't be activated and blocks the way).
            if (window == active || window->isDeleted() || !window->wantsTabFocus() || window->skipSwitcher()
                || window->isMinimized() || !window->isShown() || window->isHiddenByShowDesktop()
                || !window->readyForPainting() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }
            const QRectF drawn = currentlyDrawn(window);
            const RectF screen = window->output()->geometryF();
            if (!drawn.intersects(QRectF(screen.x(), screen.y(), screen.width(), screen.height()))) {
                continue;
            }
            const QPointF to = drawn.center();
            qreal distance;
            qreal offset;
            switch (key) {
            case Qt::Key_Left:
                distance = from.x() - to.x();
                offset = std::abs(to.y() - from.y());
                break;
            case Qt::Key_Right:
                distance = to.x() - from.x();
                offset = std::abs(to.y() - from.y());
                break;
            case Qt::Key_Up:
                distance = from.y() - to.y();
                offset = std::abs(to.x() - from.x());
                break;
            default:
                distance = to.y() - from.y();
                offset = std::abs(to.x() - from.x());
                break;
            }
            if (distance <= 0) {
                continue;
            }
            const qreal score = distance + offset + offset * offset / distance;
            if (!best || score < bestScore) {
                best = window;
                bestScore = score;
            }
        }
        if (best) {
            workspace()->activateWindow(best);
        }
    }

    // Set how a window is drawn (see applyParked, dragStep), keeping its
    // focus ring the same width on screen.
    void setDrawTransform(Window *window, const QTransform &transform)
    {
        window->windowItem()->setTransform(transform);
        if (window == m_ringWindow) {
            updateRing();
        }
    }

    // Outline the active window: a line in the accent color just outside its
    // frame, ringWidth wide on screen whatever the window's scale. It is a
    // child of the window's scene item (whose coordinates start at the
    // frame's top-left corner), so it moves, scales and stacks with it.
    void updateRing()
    {
        Window *window = workspace()->activeWindow();
        const bool wanted = window && !window->isDeleted() && (window->isNormalWindow() || window->isDialog())
            && !window->isFullScreen() && window->windowItem();
        if (!wanted || window != m_ringWindow) {
            removeRing();
        }
        if (!wanted) {
            return;
        }
        const RectF frame = window->frameGeometry();
        const RectF inner(0, 0, frame.width(), frame.height());
        const qreal scale = window->windowItem()->transform().m11();
        const QColor color = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
        const BorderOutline outline(ringWidth / (scale > 0 ? scale : 1.0), color, window->borderRadius());
        if (m_ring) {
            m_ring->setInnerRect(inner);
            m_ring->setOutline(outline);
            return;
        }
        m_ring = new OutlinedBorderItem(inner, outline, window->windowItem());
        m_ring->setZ(1000); // above the window's surfaces and title bar
        m_ringWindow = window;
    }

    // Items don't delete their children, and a child must go before its
    // parent: called at the latest when the window closes.
    void removeRing()
    {
        delete m_ring;
        m_ring = nullptr;
        m_ringWindow = nullptr;
    }

    // --- Drawing and input for managed windows ---

    // The scale a managed window's current frame is drawn at.
    qreal scaleOf(Window *window) const
    {
        return displayRect(m_parked.at(window)).width() / window->frameGeometry().width();
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
        if (parked.restoring && !parked.animating && std::abs(frame.width() - parked.original.width()) < 0.5
            && std::abs(frame.height() - parked.original.height()) < 0.5) {
            const QPointF topLeft = parked.shown.topLeft();
            m_parked.erase(it);
            setDrawTransform(window, QTransform());
            if (frame.topLeft() != topLeft) {
                window->move(topLeft);
            }
            return;
        }

        const qreal scale = scaleOf(window);
        const QPointF offset = displayRect(parked).topLeft() - frame.topLeft();
        QTransform transform;
        transform.translate(offset.x(), offset.y());
        transform.scale(scale, scale);
        setDrawTransform(window, transform);
    }

    // Where a parked window is drawn.
    QRectF drawnRect(Window *window) const
    {
        const auto frame = window->frameGeometry();
        return QRectF(displayRect(m_parked.at(window)).topLeft(), QSizeF(frame.width(), frame.height()) * scaleOf(window));
    }

    // Move a parked window's frame so that the point of the window drawn at
    // `pos` is also at `pos` in the frame. The drawing stays in place.
    void reanchor(Window *window, const QPointF &pos)
    {
        if (workspace()->moveResizeWindow() == window) {
            return;
        }
        const QPointF drawn = displayRect(m_parked.at(window)).topLeft();
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

        const QPointF drawn = displayRect(it->second).topLeft();
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

KWIN_EFFECT_FACTORY(Glance, "metadata.json")

#include "main.moc"
