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
// minScale at the screen edge (but no narrower than parkingMinWidth). Quick
// tiling by dragging to the side is turned off while the effect is loaded,
// since it uses the same edges.
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
// Keyboard (replaces KDE's quick tiling on Meta+arrows): Meta+Left/Right
// step the active window between left parking, left stash, left half of
// main, right half of main, right stash and right parking; a free window in
// main first snaps to the half on that side. A window in all of main (see
// Meta+Down) goes straight to the stash on that side, and comes back as
// wide (see stepSideways). Windows move horizontally and keep their
// vertical position. Meta+Up: the half view, a half of main at full height;
// Meta+Down: the full view, all of main at full height. Keyboard moves
// animate (the drawing glides to the new place while the app resizes).
//
// Meta+drag: moves the window like a title-bar drag (following the
// pointer, shrinking by the edge rule), with two additions. Acceleration:
// horizontally the window gets ahead of the pointer, more the longer you
// keep moving fast in one direction (gain up to leadMaxGain); reversing
// or slowing down goes back to 1:1, so corrections are precise. The
// screen edges stop it, and overshoot isn't stored. Pause to snap: holding
// still for snapDwell snaps the window to the region it is in (see
// snapTargetAt: a half of main or, in a middle band, all of main, both at
// full height; a stash; parking at the very edge). From then on the drag
// is in snapping mode: moving into another region snaps there (no sizes in
// between); releasing the mouse keeps it, releasing Meta lets it follow the
// pointer again (see leadStep).
//
// Selecting (Meta+Alt+arrows, KDE's own keys for this): activates the
// nearest window in that direction, judged by where windows are drawn (KDE's
// version uses the real frames, which are wrong for parked windows).
//
// Focus ring: the active window gets an outline in the accent color, as
// wide on screen at any scale, so it stands out also when tiny. Whenever the
// ring goes to a window by keyboard (Meta+Alt+arrows, Alt+Tab; not clicks,
// drags or apps taking the focus), the window dips like a pressed button:
// it steps through bounceFrames (100% down to 98% and back), bounceStep
// apart (see bounceRing, startBounce).
//
// Previews: hovering a parking icon (not a small stashed window) makes it
// grow in place to previewGrow times its size (at most 1:1 with the app's resized layout,
// so it stays sharp), anchored at its screen edge and centered on its spot,
// over its neighbours, which stay put and partly visible. The pointer stays
// over it, so it can still be dragged, and clicks pass through as for any
// icon. The first waits previewDelay; moving into a neighbour's spot then
// switches at once (both animate); leaving closes it after previewGrace
// (see updateHover).
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
// Meta+wheel resizes the window under the pointer in place, anchored at
// the pointer (see metaWheel): in main a real resize, in a stash a scaled
// one (the app follows when the scrolling stops). Over a parking icon it
// sizes the icon's hover preview, up to the width of the edge zone; the
// preview still closes when the pointer leaves (see resizePreview).
//
// Alt+Tab (and Meta+Tab; replaces KDE's window switcher): hunt and return,
// and the desktop map while Alt is held (see alttab.h).
//
// Minimize = park: parking is Glance's minimize. A window being minimized
// (title-bar button, taskbar, shortcut, the app) is shown again at once and
// goes to the parking area on the side nearer to it (see minimizeToParking);
// focus moves on, as for a minimize.
//
// Known gaps: touch and tablets aren't handled; in the forwarding case the
// title bar doesn't respond and the cursor shape may be wrong.

#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <input.h>
#include <input_event.h>
#include <keyboard_input.h>
#include <main.h>
#include <opengl/glutils.h>
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

#include "geometry.h"
#include "alttab.h"
#include "clips.h"
#include "declutter.h"
#include "parked.h"

#include <KGlobalAccel>

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QMatrix4x4>
#include <QMimeDatabase>
#include <QPainter>
#include <QPalette>
#include <QPluginLoader>
#include <QPointer>
#include <QProcess>
#include <QSocketNotifier>
#include <QStyleHints>
#include <QTimer>
#include <QTransform>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <chrono>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <set>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

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
        // The focus ring keeps its width on screen whatever the window's scale.
        connect(&m_parking, &ParkedWindows::transformChanged, this, [this](Window *window) {
            if (window == m_ringWindow) {
                updateRing();
            }
        });
        connect(&m_altTab, &AltTab::ringChanged, this, &Glance::updateRing);
        connect(&m_altTab, &AltTab::bounce, this, &Glance::bounceRing);
        connect(workspace(), &Workspace::windowActivated, this, &Glance::updateRing);
        connect(workspace(), &Workspace::windowAdded, &m_clips, &Clips::placeClip);

        m_snapDwell.setSingleShot(true);
        m_snapDwell.setInterval(snapDwell);
        connect(&m_snapDwell, &QTimer::timeout, this, [this]() {
            if (m_dragged && !m_snapped && (input()->keyboardModifiers() & Qt::MetaModifier)) {
                m_snapAnchor = m_dragDisplayed.center();
                m_snapPointer = input()->pointer()->pos() + QPointF(m_leadX, 0);
                m_snapped = snapTargetAt(m_dragged, m_snapAnchor);
                m_leadRun = 0;
                dragStep(m_dragged);
            }
        });

        m_previewOpen.setSingleShot(true);
        m_previewOpen.setInterval(previewDelay);
        connect(&m_previewOpen, &QTimer::timeout, this, [this]() {
            if (m_previewCandidate && isPreviewable(m_previewCandidate)) {
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
        m_wheelSettle.setSingleShot(true);
        m_wheelSettle.setInterval(wheelSettle);
        connect(&m_wheelSettle, &QTimer::timeout, this, &Glance::settleWheel);
        m_anchorLate.setSingleShot(true);
        connect(&m_anchorLate, &QTimer::timeout, this, &Glance::anchorLate);
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

        // Meta+drag activates the window, as a title-bar drag does: KDE's
        // default for it is "Move", which leaves another window active.
        m_savedCommandAll1 = options->commandAll1();
        activatingMetaDrag();
        connect(options, &Options::commandAll1Changed, this, &Glance::activatingMetaDrag);

        qInfo("glance: effect loaded");
        if (!globalAccel()) {
            qWarning("glance: KWin's kglobalaccel plugin not found: a Meta held long or used with Glance may open the launcher");
        }
    }

    ~Glance() override
    {
        input()->uninstallInputEventFilter(&m_filter);
        removeRing();
        disconnect(options, nullptr, this, nullptr);
        options->setElectricBorderTiling(m_savedTiling);
        options->setCommandAll1(m_savedCommandAll1);
        for (const QPointer<QAction> &action : m_disabledActions) {
            if (action) {
                action->setEnabled(true);
            }
        }
        if (m_dragged && m_dragged->windowItem()) {
            m_dragged->windowItem()->setTransform(QTransform());
        }
        // Back to normal: full size, where they are drawn.
        m_parking.restoreAll();
    }

    // Only take part in painting while something is scaled.
    bool isActive() const override
    {
        return !m_parking.empty() || m_dragged || m_bounce || m_altTab.mapShown() || m_clips.dragging();
    }

    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override
    {
        if (m_parking.isParked(w->window()) || w->window() == m_dragged || (w->window() == m_bounce && m_bounceScale != 1.0)) {
            data.setTransformed();
        }
        m_clips.prePaintWindow(w->window(), data);
        m_altTab.prePaintWindow(w->window(), data);
        Effect::prePaintWindow(view, w, data);
    }

    // A bouncing window: scaled to its current frame's scale around the center
    // of where it is drawn. The paint data's scale works around the window
    // item's origin (it comes before the item's position), so the
    // translation moves that center back.
    // With the Alt+Tab map up, then, every window in it is drawn where the
    // map has it (see mapped), dimmed unless selected; the others (panels,
    // notifications) fade out.
    void paintWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask,
                     const Region &deviceRegion, WindowPaintData &data) override
    {
        if (w->window() == m_bounce && m_bounceScale != 1.0) {
            const QPointF center = m_parking.currentlyDrawn(m_bounce).center() - m_bounce->windowItem()->position();
            data.setXScale(data.xScale() * m_bounceScale);
            data.setYScale(data.yScale() * m_bounceScale);
            data.translate(center.x() * (1.0 - m_bounceScale), center.y() * (1.0 - m_bounceScale));
        }
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
        if (m_dragged && m_dragAnimating) {
            dragStep(m_dragged);
        }
        m_clips.prePaintScreen(data);
        m_altTab.prePaintScreen(data);
        Effect::prePaintScreen(data);
    }

    void postPaintScreen() override
    {
        if (m_dragAnimating || m_altTab.mapShown()) {
            effects->addRepaintFull();
        }
        if (m_parking.anyAnimating()) {
            effects->addRepaintFull();
        }
        Effect::postPaintScreen();
    }

    // Meta+arrows and Meta+Alt+arrows (see the header comment).
    // Meta pressed alone and released opens KDE's launcher. Glance uses
    // Meta a lot (drag, wheel, double-click), so only a real tap does: a
    // press held longer than metaTapMax (the user meant something else and
    // let go) doesn't, nor one shorter than metaTapMin (no hand is that
    // fast: VMware Fusion sends such taps when it held Command back).
    void metaKey(const KeyboardKeyEvent *event)
    {
        if (event->state == KeyboardKeyState::Pressed) {
            m_metaDown = event->timestamp;
        } else if (event->state == KeyboardKeyState::Released) {
            const auto held = event->timestamp - m_metaDown;
            if (held < metaTapMin || held > metaTapMax) {
                cancelMetaTap();
            }
        }
    }

    // Call off the launcher for the current Meta press. KWin doesn't export
    // its own call for it (GlobalShortcutsManager::cancelModiferOnlySequence),
    // but the slot it uses is on KWin's kglobalaccel plugin, a static Qt
    // plugin, whose instance Qt hands out.
    void cancelMetaTap()
    {
        if (QObject *accel = globalAccel()) {
            QMetaObject::invokeMethod(accel, "cancelModiferOnlySequence");
        }
    }

    QObject *globalAccel()
    {
        if (!m_globalAccel) {
            for (const QStaticPlugin &plugin : QPluginLoader::staticPlugins()) {
                if (plugin.metaData().value(QLatin1String("IID")).toString().contains(QLatin1String("KGlobalAccelInterface"))) {
                    m_globalAccel = plugin.instance();
                    break;
                }
            }
        }
        return m_globalAccel;
    }

    bool onKey(KeyboardKeyEvent *event)
    {
        if (event->key == Qt::Key_Meta || event->key == Qt::Key_Super_L || event->key == Qt::Key_Super_R) {
            metaKey(event);
        }
        if (m_dragged) {
            // Meta pressed or released during a drag: switch between gesture
            // and normal drag now, once KWin has updated its modifier state.
            QTimer::singleShot(0, this, [this]() {
                if (m_dragged) {
                    dragStep(m_dragged);
                }
            });
        }
        if (m_altTab.switching() || (!m_pending && m_altTab.startsSwitch(event))) {
            return m_altTab.key(event);
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
                halfView(window);
                break;
            default:
                fullView(window);
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
        if (metaWheel(event)) {
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
                m_effect->cancelMetaTap();
            }
            return taken;
        }
        bool pointerAxis(PointerAxisEvent *event) override
        {
            const bool taken = m_effect->onAxis(event);
            if (taken && event->modifiers != Qt::NoModifier) {
                m_effect->cancelMetaTap();
            }
            return taken;
        }

    private:
        Glance *m_effect;
    };

    Filter m_filter;

    using Parked = ParkedWindows::Parked;
    ParkedWindows m_parking;
    // Where the last pointer button press was: where a drag (of a window or
    // a clip) started.
    QPointF m_lastPress;
    Clips m_clips{m_parking, m_lastPress};
    AltTab m_altTab{m_parking};
    Declutter m_declutter{m_parking};

    // The window being dragged while we draw it scaled, its original size,
    // and its scale relative to that.
    QPointer<Window> m_dragged;
    QSizeF m_dragOriginal;
    qreal m_dragScale = 1.0;
    // A stashed window keeps its size when a drag starts (see followRect):
    // the held scale, and which side of it the edge rule was on (0: not
    // known yet).
    std::optional<qreal> m_dragHold;
    qreal m_dragHoldSide = 0;

    // A Meta+drag snap's target place.
    struct Gesture
    {
        int key; // tells targets apart
        QRectF drawn; // where the window is shown meanwhile
        std::optional<Place> place = std::nullopt;
    };
    // Where the dragged window was when the drag started, where it is drawn
    // now, the current gesture target, and the glide between them.
    QPointF m_dragPress;
    QRectF m_dragStartFrame;
    qreal m_dragStartCenterY = 0;
    QRectF m_dragDisplayed;
    std::optional<Gesture> m_dragGesture;
    // Acceleration state of the current Meta+drag (see leadStep): how far
    // the window is ahead of the pointer horizontally, the direction and
    // length of the current run, movement against it so far (jitter until
    // reversalJitter), the last pointer position and recent ones (for the
    // speed); the pause-to-snap target, where the pointer was then, and
    // the timer.
    qreal m_leadX = 0;
    int m_leadDir = 0;
    qreal m_leadRun = 0;
    qreal m_leadAgainst = 0;
    QPointF m_leadLast;
    std::vector<std::pair<std::chrono::steady_clock::time_point, QPointF>> m_leadSamples;
    std::optional<Gesture> m_snapped;
    // Snapping mode: the window's center and the pointer (plus lead) at the
    // first snap; the region follows the pointer's movement from there.
    QPointF m_snapAnchor;
    QPointF m_snapPointer;
    int m_dragModeKey = -1;
    bool m_dragAnimating = false;
    QRectF m_dragAnimFrom;
    std::chrono::steady_clock::time_point m_dragAnimStart;

    // Where we last pointed the seat, and whether KWin disagreed (so we
    // forward events ourselves).
    QPointer<Window> m_target;
    bool m_forwarding = false;
    // When a parked window's frame was last moved under the pointer, and
    // the timer that lines it up where the pointer stopped (see reanchor).
    std::chrono::steady_clock::time_point m_anchoredAt;
    QTimer m_anchorLate;

    // Windows that went to a stash from all of main, to come back as wide.
    std::set<Window *> m_wasFull;

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

    // The previewed window, the one the pointer waits on, and the timers to
    // open and close previews (see updateHover).
    QPointer<Window> m_preview;
    QPointer<Window> m_previewCandidate;
    QTimer m_previewOpen;
    QTimer m_snapDwell;
    QTimer m_previewClose;
    // An icon whose preview Meta+wheel just closed: no hover preview for it
    // until the pointer leaves it.
    QPointer<Window> m_noPreview;

    // Meta+wheel on a stashed window or a preview (see metaWheel): the
    // window, and the timer that resizes the app once the scrolling stops.
    QPointer<Window> m_wheelWindow;
    QTimer m_wheelSettle;
    // When Meta went down (see metaKey).
    std::chrono::microseconds m_metaDown{};
    // KWin's kglobalaccel plugin (see cancelMetaTap).
    QPointer<QObject> m_globalAccel;

    // The focus ring (see updateRing) and the window it outlines.
    OutlinedBorderItem *m_ring = nullptr;
    QPointer<Window> m_ringWindow;
    // The window bouncing as it gets the ring, its current scale, and
    // which bounce it is (later timers of an earlier one do nothing).
    QPointer<Window> m_bounce;
    qreal m_bounceScale = 1.0;
    int m_bounceCount = 0;

    // KDE's shortcut actions we disabled, to re-enable on unload.
    std::vector<QPointer<QAction>> m_disabledActions;

    // Quick tiling setting to restore when unloaded.
    bool m_savedTiling = true;
    // Meta+left-drag's mouse command to restore when unloaded (see
    // activatingMetaDrag).
    Options::MouseCommand m_savedCommandAll1 = Options::MouseMove;

    // Meta+drag ("Move" or "Unrestricted move") also activates and raises.
    // Also when the settings are reloaded.
    void activatingMetaDrag()
    {
        if (options->commandAll1() == Options::MouseMove) {
            options->setCommandAll1(Options::MouseActivateRaiseAndMove);
        } else if (options->commandAll1() == Options::MouseUnrestrictedMove) {
            options->setCommandAll1(Options::MouseActivateRaiseAndUnrestrictedMove);
        }
    }

    void watch(Window *window)
    {
        connect(window, &Window::interactiveMoveResizeStepped, this, [this, window]() {
            dragStep(window);
        });
        connect(window, &Window::interactiveMoveResizeFinished, this, [this, window]() {
            dragFinished(window);
        });
        connect(window, &Window::frameGeometryChanged, this, [this, window]() {
            m_clips.frameChanged(window);
            m_parking.applyParked(window);
            if (window == m_ringWindow) {
                updateRing();
            }
        });
        connect(window, &Window::minimizedChanged, this, [this, window]() {
            if (window->isMinimized()) {
                minimizeToParking(window);
            }
        });
        if (window->isMinimized()) {
            // Already minimized when we see it (at load, or a window that
            // starts minimized): once it is set up.
            QTimer::singleShot(0, this, [this, window = QPointer<Window>(window)]() {
                if (window && window->isMinimized()) {
                    minimizeToParking(window);
                }
            });
        }
        connect(window, &Window::fullScreenChanged, this, [this, window]() {
            if (window == m_altTab.highlighted()) {
                updateRing();
            }
        });
        connect(window, &Window::closed, this, [this, window]() {
            if (window == m_ringWindow) {
                removeRing(); // while its parent item still exists
            }
            m_clips.closed(window);
            m_parking.closed(window);
            m_wasFull.erase(window);
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

    // --- Keyboard: stepping between places ---

    // Our Meta+arrows replace KDE's quick tiling on the same keys, and our
    // Meta+Alt+arrows its "Switch to Window" ones. Rather than hiding the
    // keys from KDE's shortcut system (which then opens the launcher when
    // Meta is released), disable KWin's actions for them: the shortcut still
    // matches, and a disabled action does nothing. Only while the effect is
    // loaded; nothing is saved to the user's settings.
    // Our Alt+Tab replaces KDE's window switcher (all its "Walk Through
    // Windows" actions, also those for the current app's windows).
    void disableKdeShortcuts()
    {
        for (const char *name : {"Window Quick Tile Left", "Window Quick Tile Right",
                                 "Window Quick Tile Top", "Window Quick Tile Bottom",
                                 "Switch Window Left", "Switch Window Right",
                                 "Switch Window Up", "Switch Window Down"}) {
            disableAction(workspace(), name);
        }
        // The switcher's actions belong to KWin's TabBox object, whose class
        // header kwin-devel doesn't install. It derives from QObject alone,
        // so its pointer is the QObject's.
        if (QObject *tabBox = reinterpret_cast<QObject *>(workspace()->tabbox())) {
            for (const char *name : {"Walk Through Windows", "Walk Through Windows (Reverse)",
                                     "Walk Through Windows Alternative", "Walk Through Windows Alternative (Reverse)",
                                     "Walk Through Windows of Current Application",
                                     "Walk Through Windows of Current Application (Reverse)",
                                     "Walk Through Windows of Current Application Alternative",
                                     "Walk Through Windows of Current Application Alternative (Reverse)"}) {
                disableAction(tabBox, name);
            }
        }
    }

    void disableAction(QObject *owner, const char *name)
    {
        QAction *action = owner->findChild<QAction *>(QString::fromLatin1(name));
        if (!action) {
            qWarning("glance: KWin action \"%s\" not found", name);
            return;
        }
        if (action->isEnabled()) {
            action->setEnabled(false);
            m_disabledActions.push_back(action);
        }
    }



    // One step towards `side` along: parking L, stash L, half L, half R,
    // stash R, parking R. A free window goes to the half on that side. A
    // window in all of main goes straight to the stash on that side, and
    // from there back into all of main (m_wasFull).
    void stepSideways(Window *window, Side side)
    {
        const bool left = side == Side::Left;
        const Place from = m_parking.placeOf(window);
        Place to;
        if (from == Place::Free) {
            to = left ? Place::HalfLeft : Place::HalfRight;
        } else if (from == Place::Full) {
            to = left ? Place::StashLeft : Place::StashRight;
            m_wasFull.insert(window);
        } else {
            const int i = placeIndex(from);
            const int j = std::clamp(i + (left ? -1 : 1), 0, 5);
            if (i == j) {
                return;
            }
            to = placeOrder[j];
            const bool backIntoMain = (from == Place::StashLeft && to == Place::HalfLeft)
                || (from == Place::StashRight && to == Place::HalfRight);
            if (backIntoMain && m_wasFull.erase(window)) {
                to = Place::Full;
            } else if (from == Place::HalfLeft || from == Place::HalfRight) {
                m_wasFull.erase(window);
            }
        }
        m_parking.moveTo(window, to);
    }

    // Minimize = park: show a minimized window again and put it in the
    // parking area on the side nearer to where it is drawn (one already
    // there stays). Called while the minimize is under way: KWin has moved
    // focus on (wanted: minimize means out of the way) and minimized its
    // dialogs, which come back with it. KDE's minimize animation (Squash)
    // is reversed before it starts, so nothing flashes. Windows Meta+arrows
    // can't move (full screen, fixed size, not normal) minimize as usual.
    void minimizeToParking(Window *window)
    {
        if (!window->isNormalWindow() || window->isFullScreen() || !window->isMovable() || !window->isResizable()
            || !window->windowItem() || workspace()->moveResizeWindow() == window) {
            return;
        }
        window->setMinimized(false);
        if (window->isMinimized()) {
            return; // a window rule keeps it minimized
        }
        const Place place = m_parking.placeOf(window);
        if (place == Place::ParkingLeft || place == Place::ParkingRight) {
            qInfo("glance: minimize: already parked: %s", qPrintable(window->caption()));
            return;
        }
        const RectF screen = window->output()->geometryF();
        const bool left = m_parking.currentlyDrawn(window).center().x() < screen.x() + screen.width() / 2;
        m_parking.moveTo(window, left ? Place::ParkingLeft : Place::ParkingRight);
        qInfo("glance: minimize -> parking %s: %s", left ? "left" : "right", qPrintable(window->caption()));
    }

    // Meta+Up, the half view: a half of main at full height. A window in a
    // half stays in it; one in all of main goes to a free half (see
    // freeHalf); any other to the half nearer to it. Not for parked windows.
    void halfView(Window *window)
    {
        if (m_parking.isParkedNotRestoring(window)) {
            return;
        }
        const Place place = m_parking.placeOf(window);
        Place half = place;
        if (place == Place::Full) {
            half = freeHalf(window);
        } else if (place != Place::HalfLeft && place != Place::HalfRight) {
            const RectF screen = window->output()->geometryF();
            half = m_parking.currentlyDrawn(window).center().x() < screen.x() + screen.width() / 2 ? Place::HalfLeft : Place::HalfRight;
        }
        m_parking.fillHalf(window, half);
    }

    // Meta+Down, the full view: all of main at full height. Not for parked
    // windows.
    void fullView(Window *window)
    {
        if (m_parking.isParkedNotRestoring(window)) {
            return;
        }
        releaseKdeState(window);
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const qreal zoneWidth = screen.width() * zoneFraction;
        m_parking.resizeAnimated(window, RectF(screen.x() + zoneWidth, area.y(), 2 * zoneWidth, area.height()), m_parking.currentlyDrawn(window));
    }

    // The half of main for `window`: the free one if the other is taken by
    // another window, else (both free or both taken) the left one.
    Place freeHalf(Window *window) const
    {
        bool taken[2] = {false, false};
        for (Window *other : workspace()->stackingOrder()) {
            if (other == window || !manageable(other) || other->output() != window->output() || m_parking.isParkedNotRestoring(other)) {
                continue;
            }
            const Place place = m_parking.placeOf(other);
            if (place == Place::HalfLeft) {
                taken[0] = true;
            } else if (place == Place::HalfRight) {
                taken[1] = true;
            }
        }
        return taken[0] && !taken[1] ? Place::HalfRight : Place::HalfLeft;
    }

    // --- Dragging ---

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
            auto *it = m_parking.find(window);
            const bool parked = it && !it->restoring;
            m_dragOriginal = it ? it->original : QSizeF(frame.width(), frame.height());
            m_dragPress = m_lastPress;
            const QPointF moved = cursor - m_dragPress;
            m_dragStartFrame = QRectF(frame.x() - moved.x(), frame.y() - moved.y(), frame.width(), frame.height());
            m_dragStartCenterY = parked ? it->shown.center().y() : m_dragStartFrame.center().y();
            m_dragDisplayed = m_parking.currentlyDrawn(window);
            m_dragHold.reset();
            m_dragHoldSide = 0;
            if (parked && !m_parking.isParkingArea(m_parking.areaOf(window))) {
                m_dragHold = it->shown.width() / it->original.width();
            }
            m_dragGesture.reset();
            m_dragModeKey = -1;
            m_leadX = 0;
            m_leadDir = 0;
            m_leadRun = 0;
            m_leadAgainst = 0;
            m_leadLast = cursor;
            m_leadSamples.clear();
            m_snapped.reset();
            m_snapDwell.stop();
            m_dragAnimating = false;
            const auto closeRanks = m_parking.leaving(window);
            m_parking.erase(window);
            m_dragged = window;
            closeRanks();
        }

        const std::optional<Gesture> gesture = leadStep(window, cursor);
        // The edge rule around where the window is: the pointer plus the
        // window's lead (the frame shifted with it keeps the grab offset).
        const QRectF want = gesture ? gesture->drawn
                                    : followRect(window, RectF(frame.x() + m_leadX, frame.y(), frame.width(), frame.height()),
                                                 cursor + QPointF(m_leadX, 0));
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
            const qreal t = std::clamp(std::chrono::duration<qreal>(elapsed) / glance::animationTime, 0.0, 1.0);
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
        m_parking.setDrawTransform(window, transform);
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
        total = std::max(total, parkingScale(m_dragOriginal));
        if (m_dragHold) {
            total = holdScale(frame, cursor, total, screen);
        }
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

    // A window dragged out of a stash keeps the size it had there (set by
    // the drop or by Meta+wheel), so the drag doesn't start with a jump:
    // the edge rule (`rule`) takes over once it reaches that size (no jump
    // then), or with a glide once the window's center leaves the stash.
    qreal holdScale(const RectF &frame, const QPointF &cursor, qreal rule, const RectF &screen)
    {
        const qreal hold = *m_dragHold;
        const qreal side = rule - hold;
        if (m_dragHoldSide == 0) {
            m_dragHoldSide = side < 0 ? -1 : 1;
        }
        if (side * m_dragHoldSide <= 0) {
            m_dragHold.reset();
            return rule;
        }
        const qreal scale = hold * m_dragOriginal.width() / frame.width();
        const qreal centerX = cursor.x() + (frame.x() + frame.width() / 2 - cursor.x()) * scale - screen.x();
        const qreal zoneWidth = screen.width() * zoneFraction;
        const qreal band = zoneWidth * parkingBand;
        const bool inStash = (centerX >= band && centerX < zoneWidth)
            || (centerX <= screen.width() - band && centerX > screen.width() - zoneWidth);
        if (!inStash) {
            m_dragHold.reset();
            m_dragAnimFrom = m_dragDisplayed;
            m_dragAnimStart = std::chrono::steady_clock::now();
            m_dragAnimating = true;
            return rule;
        }
        return hold;
    }

    // Acceleration and snapping, on every drag step: updates the window's
    // lead (see updateLead) and returns the snap target, if snapping. The
    // first snap comes from m_snapDwell (a pause); then, in snapping mode,
    // the target follows the region the window would be in (the pointer's
    // movement since the snap, added to the window's center then). Without
    // Meta the gain is 1 (the lead stays) and there is no snapping.
    std::optional<Gesture> leadStep(Window *window, const QPointF &cursor)
    {
        const auto now = std::chrono::steady_clock::now();
        const bool moved = cursor != m_leadLast;
        const qreal dx = cursor.x() - m_leadLast.x();
        m_leadLast = cursor;
        if (moved) {
            m_leadSamples.emplace_back(now, cursor);
            std::erase_if(m_leadSamples, [&](const auto &sample) {
                return now - sample.first > leadSampleTime;
            });
        }
        if (!(input()->keyboardModifiers() & Qt::MetaModifier)) {
            m_snapDwell.stop();
            m_leadRun = 0;
            m_snapped.reset();
            return std::nullopt;
        }
        if (moved) {
            updateLead(window, cursor, dx, now);
            if (!m_snapped) {
                m_snapDwell.start();
            }
        }
        if (m_snapped) {
            const QPointF point = m_snapAnchor + (cursor + QPointF(m_leadX, 0) - m_snapPointer);
            if (auto target = snapTargetAt(window, point); target->key != m_snapped->key) {
                m_snapped = target;
            }
        }
        return m_snapped;
    }

    // Acceleration: each horizontal pointer movement moves the window that
    // much times the gain, which grows from 1 to leadMaxGain over the run
    // (movement in one direction); a reversal (reversalJitter the other way)
    // or moving slower than leadSlow starts a new run at 1. The lead keeps
    // the window between the screen edges, so overshoot isn't stored.
    void updateLead(Window *window, const QPointF &cursor, qreal dx, std::chrono::steady_clock::time_point now)
    {
        // Slow: 1:1 (precise).
        const auto &[t0, p0] = m_leadSamples.front();
        const qreal dt = std::chrono::duration<qreal>(now - t0).count();
        if (dt <= 0 || std::abs(cursor.x() - p0.x()) / dt < leadSlow) {
            m_leadRun = 0;
            m_leadAgainst = 0;
            return;
        }
        if (dx == 0) {
            return;
        }
        const RectF screen = window->moveResizeOutput()->geometryF();
        const int dir = dx < 0 ? -1 : 1;
        qreal gain = 1.0;
        if (m_leadDir == 0 || dir == m_leadDir) {
            m_leadDir = dir;
            m_leadRun += std::abs(dx);
            m_leadAgainst = 0;
            gain = 1.0 + (leadMaxGain - 1.0) * std::min(1.0, m_leadRun / (screen.width() * leadBuild));
        } else {
            m_leadAgainst += std::abs(dx);
            if (m_leadAgainst >= reversalJitter) {
                m_leadDir = dir; // reversed: a new run, at 1:1
                m_leadRun = m_leadAgainst;
                m_leadAgainst = 0;
            }
        }
        m_leadX += (gain - 1.0) * dx;
        const qreal x = std::clamp(cursor.x() + m_leadX, screen.x(), screen.x() + screen.width());
        m_leadX = x - cursor.x();
    }

    // The snap target for a window centered at `point`, by region: in the
    // parking band parking, elsewhere in an edge zone the stash, in main a
    // half, or all of main in the middle band (snapFullBand); halves and all
    // of main at full height.
    std::optional<Gesture> snapTargetAt(Window *window, const QPointF &point) const
    {
        const RectF screen = window->moveResizeOutput()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const qreal zoneWidth = screen.width() * zoneFraction;
        const qreal x = point.x() - screen.x();
        const qreal width = screen.width();
        Place place;
        if (x < zoneWidth * parkingBand) {
            place = Place::ParkingLeft;
        } else if (x < zoneWidth) {
            place = Place::StashLeft;
        } else if (x > width - zoneWidth * parkingBand) {
            place = Place::ParkingRight;
        } else if (x > width - zoneWidth) {
            place = Place::StashRight;
        } else if (x < width / 2 - width * snapFullBand / 2) {
            place = Place::HalfLeft;
        } else if (x > width / 2 + width * snapFullBand / 2) {
            place = Place::HalfRight;
        } else {
            place = Place::Full;
        }
        QRectF drawn;
        switch (place) {
        case Place::HalfLeft:
        case Place::HalfRight:
            drawn = QRectF(screen.x() + (place == Place::HalfLeft ? zoneWidth : 2 * zoneWidth), area.y(), zoneWidth, area.height());
            break;
        case Place::Full:
            drawn = QRectF(screen.x() + zoneWidth, area.y(), 2 * zoneWidth, area.height());
            break;
        default:
            drawn = m_parking.placeRect(window, place, m_dragOriginal, point.y());
            break;
        }
        return Gesture{.key = int(place), .drawn = drawn, .place = place};
    }

    // Released with a gesture target: go there, gliding from where it is
    // shown.
    void commitGesture(Window *window, const Gesture &gesture, const QRectF &from)
    {
        if (!gesture.place) {
            return;
        }
        switch (*gesture.place) {
        case Place::HalfLeft:
        case Place::HalfRight:
        case Place::Full: {
            // At full height: exactly the target rectangle.
            const QRectF &r = gesture.drawn;
            releaseKdeState(window);
            m_parking.resizeAnimated(window, RectF(r.x(), r.y(), r.width(), r.height()), from);
            break;
        }
        default:
            m_parking.commitPlace(window, *gesture.place, m_dragOriginal, gesture.drawn.center().y(), from);
            break;
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
        m_snapDwell.stop();
        m_snapped.reset();
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
            m_parking.park(window, drawn, m_dragOriginal);
            m_parking.arrange(window);
        } else if (m_dragOriginal != QSizeF(frame.width(), frame.height())) {
            // Back to the original size, keeping the grabbed spot under the
            // cursor (plus the window's lead): the drawing grows around it
            // until the app has resized.
            const QPointF cursor = input()->pointer()->pos();
            const QPointF lead(m_leadX, 0);
            const qreal grow = m_dragOriginal.width() / frame.width();
            const QRectF target(cursor + lead - (cursor - frame.topLeft()) * grow, m_dragOriginal);
            m_parking.set(window, Parked{.shown = target, .original = m_dragOriginal, .restoring = true});
            qInfo("glance: %s: restore to %.0fx%.0f", qPrintable(window->caption()),
                  m_dragOriginal.width(), m_dragOriginal.height());
            window->moveResize(RectF(target.topLeft(), m_dragOriginal));
            m_parking.applyParked(window);
        } else {
            // Full size: where it is drawn (ahead of the pointer by the lead).
            if (m_leadX != 0) {
                window->move(frame.topLeft() + QPointF(m_leadX, 0));
            }
            m_parking.setDrawTransform(window, QTransform());
        }
    }

    // --- Hover previews ---

    // The previewed window, if it still is one.
    Window *previewWindow()
    {
        if (m_preview && !(m_parking.isParked(m_preview) && m_parking.at(m_preview).preview)) {
            m_preview = nullptr;
        }
        return m_preview;
    }

    // The parking icon whose home spot in its column is at `pos` (the spot counts
    // also while that window is out as a preview), half the gap around it
    // included so moving along the column never falls between two.
    Window *iconAt(const QPointF &pos) const
    {
        for (const auto &[window, parked] : m_parking.all()) {
            if (isPreviewable(window) && !window->isMinimized() && window->isOnCurrentDesktop()
                && parked.shown.adjusted(0, -arrangeGap / 2, 0, arrangeGap / 2).contains(pos)) {
                return window;
            }
        }
        return nullptr;
    }

    // Previews are for parking icons only: a stashed window can be as small
    // as an icon (dropped near the edge) but isn't one to preview.
    bool isPreviewable(Window *window) const
    {
        return m_parking.isIcon(window) && m_parking.isParkingArea(m_parking.areaOf(window));
    }

    // On every pointer motion: the icon whose home spot is under the pointer
    // grows after previewDelay, or at once if another is grown already (that
    // one shrinks at the same time), also where the grown one covers that
    // spot. Elsewhere over the grown one it stays; anywhere else it shrinks
    // after previewGrace. Nothing changes while a button is held, a window is
    // moved, or something is dragged.
    void updateHover(const QPointF &pos, Qt::MouseButtons buttons)
    {
        if (buttons != Qt::NoButton || workspace()->moveResizeWindow() || waylandServer()->seat()->isDrag()) {
            m_previewOpen.stop();
            return;
        }
        Window *preview = previewWindow();
        Window *icon = iconAt(pos);
        if (m_noPreview && icon != m_noPreview) {
            m_noPreview = nullptr;
        }
        if (icon && icon == m_noPreview) {
            m_previewOpen.stop();
            m_previewClose.stop();
            return;
        }
        if (!icon && preview && m_parking.drawnRect(preview).contains(pos)) {
            m_previewOpen.stop();
            m_previewClose.stop();
            return;
        }
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

    // In place: previewGrow times its home spot (at most 1:1 with the app's
    // current layout, and the screen height), at its screen edge, centered
    // vertically on its spot.
    QRectF previewRect(Window *window) const
    {
        const Parked &parked = m_parking.at(window);
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const RectF frame = window->frameGeometry();
        const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
        const qreal scale = std::min({previewGrow * parked.shown.width() / frame.width(), 1.0,
                                      area.height() / frame.height()});
        const QSizeF size = QSizeF(frame.width(), frame.height()) * scale;
        const qreal x = left ? parked.shown.left() : parked.shown.right() - size.width();
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
        const QRectF from = m_parking.currentlyDrawn(window);
        m_parking.at(window).preview = previewRect(window);
        m_preview = window;
        workspace()->raiseWindow(window);
        m_parking.animate(window, from);
    }

    void closePreview(Window *window)
    {
        const QRectF from = m_parking.currentlyDrawn(window);
        Parked &parked = m_parking.at(window);
        parked.preview.reset();
        if (window == m_preview) {
            m_preview = nullptr;
        }
        shrinkApp(window);
        m_parking.animate(window, from);
    }

    // A preview enlarged with Meta+wheel may have resized the app (see
    // settleWheel): back to its parked layout size.
    void shrinkApp(Window *window)
    {
        if (m_wheelWindow == window) {
            m_wheelSettle.stop();
            m_wheelWindow = nullptr;
        }
        const Parked &parked = m_parking.at(window);
        const RectF frame = window->frameGeometry();
        if (isClip(window) || parked.restoring) {
            return;
        }
        const QSizeF layout = m_parking.layoutSize(window, parked.shown.size(), parked.original);
        if (layout.width() < frame.width() - 0.5) {
            window->moveResize(RectF(parked.shown.topLeft(), layout));
        }
    }

    // --- Meta+wheel: resizing in place ---

    // Meta+wheel (vertical) over a window grows it (scrolling up) or shrinks
    // it, anchored at the pointer: the point under it stays put. In main the
    // app is really resized at once. In a stash it is drawn larger or
    // smaller (between just above parking size and full size), and the app
    // gets the new size once the scrolling stops (wheelSettle). Over a
    // parking icon it sizes the icon's preview (see resizePreview). Returns
    // whether the event was taken.
    bool metaWheel(PointerAxisEvent *event)
    {
        if (event->modifiers != Qt::MetaModifier || event->orientation != Qt::Vertical || event->delta == 0
            || event->buttons != Qt::NoButton || workspace()->moveResizeWindow() || waylandServer()->seat()->isDrag()) {
            return false;
        }
        Window *window = m_parking.pick(event->position);
        if (!window || !manageable(window)) {
            return false;
        }
        const qreal step = event->source == PointerAxisSource::Wheel ? wheelStepWheel : wheelStepFinger;
        const qreal factor = std::exp(-event->delta * step);
        auto *it = m_parking.find(window);
        if (!it) {
            resizeInMain(window, factor, event->position);
        } else if (it->restoring) {
            return true; // on its way somewhere: wait
        } else if (m_parking.isParkingArea(m_parking.areaOf(window))) {
            resizePreview(window, factor, event->position);
        } else {
            resizeInStash(window, factor, event->position);
        }
        return true;
    }

    void resizeInMain(Window *window, qreal factor, const QPointF &pos)
    {
        const RectF frame = window->moveResizeGeometry();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const QSizeF appMin = window->clientSizeToFrameSize(window->minSize());
        const QSizeF from(frame.width(), frame.height());
        // Both sides by the same factor, each within its own limits.
        const QSizeF to(std::clamp(from.width() * factor, std::max(wheelMinWidth, appMin.width()), std::max(area.width(), appMin.width())),
                        std::clamp(from.height() * factor, std::max(wheelMinHeight, appMin.height()), std::max(area.height(), appMin.height())));
        if (std::abs(to.width() - from.width()) < 0.5 && std::abs(to.height() - from.height()) < 0.5) {
            return;
        }
        releaseKdeState(window);
        const QRectF target = keptIn(QRectF(scaledTopLeft(QPointF(frame.x(), frame.y()), pos, from, to), to), area);
        window->moveResize(RectF(target.x(), target.y(), target.width(), target.height()));
    }

    void resizeInStash(Window *window, qreal factor, const QPointF &pos)
    {
        Parked &parked = m_parking.at(window);
        const qreal scale = parked.shown.width() / parked.original.width();
        const qreal want = std::clamp(scale * factor, parkingScale(parked.original) + 0.03, parkBelow - 0.01);
        if (std::abs(want - scale) < 1e-4) {
            return;
        }
        if (m_wheelWindow && m_wheelWindow != window) {
            settleWheel();
        }
        const QSizeF size = parked.shown.size() * (want / scale);
        const QRectF from = m_parking.displayRect(parked);
        parked.shown = keptIn(QRectF(scaledTopLeft(parked.shown.topLeft(), pos, parked.shown.size(), size), size),
                              window->output()->geometryF());
        if (parked.preview) { // a small stashed window can be previewed
            parked.preview.reset();
            m_preview = nullptr;
            m_noPreview = window;
        }
        m_parking.animate(window, from);
        m_wheelWindow = window;
        m_wheelSettle.start();
    }

    // The scrolling stopped: a stashed window's app is resized to fit (see
    // park); a preview grown past its app's size gets the app resized to
    // it, so it stays sharp (until the preview closes, see shrinkApp).
    void settleWheel()
    {
        m_wheelSettle.stop();
        if (Window *window = m_wheelWindow; window && m_parking.isParkedNotRestoring(window)) {
            const Parked parked = m_parking.at(window);
            if (!m_parking.isParkingArea(m_parking.areaOf(window))) {
                m_parking.park(window, parked.shown, parked.original);
            } else if (parked.preview && parked.preview->width() > window->frameGeometry().width() + 0.5) {
                window->moveResize(RectF(parked.preview->topLeft(), parked.preview->size().toSize()));
            }
        }
        m_wheelWindow = nullptr;
    }

    // Over a parking icon: its hover preview grows or shrinks (opening at
    // once if it isn't open), anchored at its screen edge and vertically at
    // the pointer, from icon size up to the width of the edge zone (so it
    // covers the stash but never main), the screen height, and the
    // original size; clips (whose layout is their own) up to 1:1. It is
    // still a preview: it closes when the pointer leaves, so nothing stays
    // in the way. Shrunk back to icon size it closes, and doesn't open
    // again until the pointer leaves the icon. Grown past the app's size,
    // the app follows once the scrolling stops (settleWheel).
    void resizePreview(Window *window, qreal factor, const QPointF &pos)
    {
        Parked &parked = m_parking.at(window);
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const RectF frame = window->frameGeometry();
        const QRectF current = parked.preview ? *parked.preview : parked.shown;
        const qreal aspect = current.height() / current.width();
        const qreal minWidth = parked.shown.width();
        const qreal maxWidth = std::max(minWidth, isClip(window) ? frame.width()
                                                                 : std::min({screen.width() * zoneFraction, area.height() / aspect,
                                                                             parked.original.width()}));
        const qreal width = std::clamp(current.width() * factor, minWidth, maxWidth);
        if (std::abs(width - current.width()) < 0.5) {
            return;
        }
        m_previewOpen.stop();
        m_previewClose.stop();
        if (width <= minWidth + 0.5) {
            if (parked.preview) {
                closePreview(window);
            }
            m_noPreview = window;
            return;
        }
        if (m_wheelWindow && m_wheelWindow != window) {
            settleWheel();
        }
        if (Window *old = previewWindow(); old && old != window) {
            closePreview(old);
        }
        const bool left = parked.shown.center().x() < screen.x() + screen.width() / 2;
        const QSizeF size(width, width * aspect);
        const qreal anchor = std::clamp((pos.y() - current.y()) / current.height(), 0.0, 1.0);
        const qreal y = std::clamp(pos.y() - anchor * size.height(), area.y(),
                                   std::max(area.y(), area.y() + area.height() - size.height()));
        const QRectF from = m_parking.currentlyDrawn(window);
        parked.preview = QRectF(QPointF(left ? parked.shown.left() : parked.shown.right() - size.width(), y), size);
        m_preview = window;
        m_previewCandidate = nullptr;
        m_noPreview = nullptr;
        workspace()->raiseWindow(window);
        m_parking.animate(window, from);
        if (!isClip(window)) {
            m_wheelWindow = window;
            m_wheelSettle.start();
        }
    }

    // --- Selecting and the focus ring ---

    // Meta+Alt+arrow: activate the nearest window in that direction, by
    // where windows are drawn (centers), scored like KWin's own
    // Workspace::switchWindow: distance along the arrow, plus how far off
    // to the side, plus a penalty for being far off to the side but close.
    void selectToward(Qt::Key key)
    {
        Window *active = workspace()->activeWindow();
        const QPointF from = active ? m_parking.currentlyDrawn(active).center() : input()->pointer()->pos();
        Window *best = nullptr;
        qreal bestScore = 0;
        for (Window *window : workspace()->stackingOrder()) {
            if (window == active || !m_parking.switchable(window)) {
                continue;
            }
            const QPointF to = m_parking.currentlyDrawn(window).center();
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
            bounceRing(best);
        }
    }

    // Outline the highlighted window: a line in the accent color just outside
    // its frame, ringWidth wide on screen whatever the window's scale (also
    // in the Alt+Tab map). It is a child of the window's scene item (whose
    // coordinates start at the frame's top-left corner), so it moves, scales
    // and stacks with it.
    void updateRing()
    {
        Window *window = m_altTab.highlighted();
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
        m_ringWindow = window;
    }

    // The ring moved by keyboard (Alt+Tab, Meta+Alt+arrows): it bounces
    // there. Clicks, drags and apps taking the focus don't bounce: the
    // user's eyes are already on the window (user, 2026-10-03: bouncing
    // on every focus change, e.g. during text drags, felt busy).
    void bounceRing(Window *window)
    {
        updateRing();
        if (window && window == m_ringWindow) {
            startBounce(window);
        }
    }

    // Items don't delete their children, and a child must go before its
    // parent: called at the latest when the window closes.
    void removeRing()
    {
        delete m_ring;
        m_ring = nullptr;
        m_ringWindow = nullptr;
    }

    // Bounce `window` like a pressed button: through bounceFrames, one every
    // bounceStep (see paintWindow). Stepped, not animated in between.
    void startBounce(Window *window)
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
                if (i == frames - 1) {
                    m_bounce = nullptr;
                }
                effects->addRepaintFull();
            });
        }
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
