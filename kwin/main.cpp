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
// ~/Clips and opened in glance-clip (our clip app, kwin/clip) where it was
// dropped, as if its window had
// been dragged there (held at its center), so it can be moved, parked and
// selected like any window (see dropToClip). Dropped in the parking band
// (the outer parkingBand of an edge zone, also onto parking icons), it
// becomes a parking icon in that column instead. Meta+C (a KDE global
// shortcut, changeable in System Settings) clips the text selected in the
// active window the same way, into parking on the side nearer that window
// (see clipSelection). Dragging a clip's body drags its text out, drawn as
// if the note were being moved: an app that takes it gets it pasted and the
// clip is gone (Shift: copied); on the desktop the note moves there; refused,
// it slides back (see clipDragStarted).
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
// Alt+Tab (and Meta+Tab; replaces KDE's window switcher): hunt and return.
// Windows in the order they were last used; a quick Alt+Tab goes back to the
// previous one, so two windows toggle with a tap. Holding Alt shows the map:
// the whole desktop drawn at mapScale in the middle of the screen, same
// layout, dimmed except the selected window, overlapping windows spread into
// rows above and below their pile; a label at the bottom of the selected
// window names it (icon and title). Tab / Shift+Tab move the selection, releasing Alt
// focuses it where it is, Esc cancels. Nothing moves: only the drawing
// changes (see switchKey, openMap, spreadPiles).
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

        // Until windows get used, the front one counts as the most recent.
        const auto &stacking = workspace()->stackingOrder();
        m_recent.assign(stacking.rbegin(), stacking.rend());
        noteActivated(workspace()->activeWindow());
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
        connect(workspace(), &Workspace::windowActivated, this, &Glance::noteActivated);
        connect(workspace(), &Workspace::windowActivated, this, &Glance::updateRing);
        connect(workspace(), &Workspace::windowAdded, this, &Glance::placeClip);
        auto seat = waylandServer()->seat();
        connect(seat, &SeatInterface::dragStarted, this, &Glance::clipDragStarted);
        connect(seat, &SeatInterface::dragDropped, this, [this]() {
            if (m_clipDrag) {
                m_clipDrag->dropped = true;
                m_clipDrag->copy = input()->keyboardModifiers() & Qt::ShiftModifier;
            }
        });
        connect(seat, &SeatInterface::dragEnded, this, &Glance::clipDragEnded);

        auto clipAction = new QAction(this);
        clipAction->setObjectName(QStringLiteral("Glance Clip Selection"));
        clipAction->setText(QStringLiteral("Glance: Clip the Selected Text"));
        KGlobalAccel::self()->setGlobalShortcut(clipAction, QKeySequence(Qt::META | Qt::Key_C));
        connect(clipAction, &QAction::triggered, this, &Glance::clipSelection);

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
        m_hold.setSingleShot(true);
        m_hold.setInterval(holdDelay);
        connect(&m_hold, &QTimer::timeout, this, [this]() {
            if (m_switch) {
                openMap();
            }
        });
        updateRing();
        // For checking the map without a keyboard (e.g. in a headless KWin
        // with a screenshot): GLANCE_TEST_MAP=1 opens it 3 s after loading.
        if (qEnvironmentVariableIsSet("GLANCE_TEST_MAP")) {
            QTimer::singleShot(3000, this, [this]() {
                startSwitch(Qt::AltModifier);
                step(1); // the hold timer then opens the map
            });
        }

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
        if (m_switch) {
            input()->keyboard()->update(); // give the keyboard back
        }
        // A texture can only be deleted with its GL context current.
        if (m_label && !effects->makeOpenGLContextCurrent()) {
            (void)m_label.release();
        }
        m_label.reset();
        if (m_clip) {
            m_clip->notifier.reset();
            close(m_clip->fd);
        }
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
        return !m_parking.empty() || m_dragged || m_bounce || m_map || m_clipDrag;
    }

    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override
    {
        if (m_parking.isParked(w->window()) || w->window() == m_dragged || (w->window() == m_bounce && m_bounceScale != 1.0)) {
            data.setTransformed();
        }
        if (m_clipDrag && w->window() == m_clipDrag->window) {
            data.setTransformed();
        }
        if (m_map) {
            data.setTransformed();
            if (!inMap(w->window())) {
                data.setTranslucent(); // fades out
            }
        }
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
        if (m_map) {
            Window *window = w->window();
            if (inMap(window) && window->windowItem() && m_parking.currentlyDrawn(window).width() > 0) {
                const QRectF from = m_parking.currentlyDrawn(window);
                retarget(data, window, from, mapped(window, from));
                if (window != mapSelected()) {
                    data.multiplyBrightness(1.0 - (1.0 - mapDim) * m_mapOpen);
                }
            } else {
                data.multiplyOpacity(1.0 - m_mapOpen);
            }
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
        if (m_clipDrag) {
            // A clip being dragged is drawn under the pointer (see
            // clipGhostRect). The whole screen is painted as transformed
            // meanwhile, so every window needs a finite region (see above).
            Window *window = w->window();
            if (window == m_clipDrag->window && window->windowItem() && m_parking.currentlyDrawn(window).width() > 0) {
                retarget(data, window, m_parking.currentlyDrawn(window), m_clipDrag->ghost);
            }
            Effect::paintWindow(renderTarget, viewport, w, mask, Region(viewport.deviceRect()), data);
            return;
        }
        Effect::paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
    }

    // Change paint data so the window drawn at `from` is drawn at `to`
    // (same shape). It is drawn at `item` + translation + scale * (item-
    // local point), so this goes on top of whatever the data does already.
    static void retarget(WindowPaintData &data, Window *window, const QRectF &from, const QRectF &to)
    {
        const qreal k = to.width() / from.width();
        const QPointF item = window->windowItem()->position();
        data.setXScale(data.xScale() * k);
        data.setYScale(data.yScale() * k);
        data.setXTranslation(to.x() - item.x() + k * (item.x() + data.xTranslation() - from.x()));
        data.setYTranslation(to.y() - item.y() + k * (item.y() + data.yTranslation() - from.y()));
    }

    // The Alt+Tab label, over everything.
    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const Region &deviceRegion,
                     LogicalOutput *screen) override
    {
        Effect::paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
        if (m_map && m_mapOpen > 0.0 && effects->isOpenGLCompositing()) {
            paintLabel(renderTarget, viewport, screen);
        }
    }

    // Advance animations: each frame, redraw animating windows at their
    // current place; finished ones settle.
    void prePaintScreen(ScreenPrePaintData &data) override
    {
        m_parking.advance();
        if (m_dragged && m_dragAnimating) {
            dragStep(m_dragged);
        }
        if (m_clipDrag) {
            data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
        }
        if (m_map) {
            m_mapOpen = mapProgress();
            if (m_map->closing && m_mapOpen <= 0.0) {
                m_map.reset();
                effects->addRepaintFull();
            } else {
                data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
            }
            updateRing(); // its width follows the map's scale
        }
        Effect::prePaintScreen(data);
    }

    void postPaintScreen() override
    {
        if (m_dragAnimating || m_map) {
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
        if (m_switch || startsSwitch(event)) {
            return switchKey(event);
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
        if (m_switch || m_map) {
            return true; // the pointer does nothing while switching
        }
        if (m_clipDrag) {
            if (!m_clipDrag->dropped) {
                m_clipDrag->ghost = clipGhostRect(event->position);
                effects->addRepaintFull();
            }
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
        if (pressed && (m_switch || m_map)) {
            return true;
        }
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
        if (m_switch || m_map) {
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
    QPointF m_lastPress;
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

    // Text or an image being turned into a clip (see startClip): the type
    // asked for, the data read so far, where the clip goes, and for a drop
    // (see dropToClip) the held-back release.
    struct ClipRead
    {
        int fd = -1;
        std::unique_ptr<QSocketNotifier> notifier = nullptr;
        QString mimeType = {};
        QByteArray text = {};
        QPointF position;
        std::optional<Place> place = std::nullopt;
        bool fromDrag = false;
        quint32 nativeButton = 0;
        std::chrono::microseconds timestamp = {};
    };
    std::unique_ptr<ClipRead> m_clip;
    // A clip being dragged (see clipDragStarted): its window, where it was
    // grabbed (fraction of its drawn size), its full size, its shape, where
    // it is drawn now, and whether it was dropped on an app that took it
    // (with Shift: copied).
    struct ClipDrag
    {
        QPointer<Window> window;
        QPointF grab;
        QSizeF original;
        qreal aspect = 1;
        QRectF ghost;
        bool dropped = false;
        bool copy = false;
    };
    std::optional<ClipDrag> m_clipDrag;

    // The clip app started for the last clip, where its window goes, and
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

    // Alt+Tab: windows, most recently used first (see noteActivated; KWin's
    // own focus chain isn't exported to plugins).
    std::vector<Window *> m_recent;
    // A switch in progress (see switchKey): the windows in recency order
    // when it started, the selected one (-1 before the first Tab) and the
    // modifier it is held with (Alt or Meta). The timer shows the map.
    struct Switch
    {
        std::vector<QPointer<Window>> windows;
        int index = -1;
        Qt::KeyboardModifier modifier = Qt::AltModifier;
    };
    std::optional<Switch> m_switch;
    QPointer<Window> m_chosen;
    QTimer m_hold;
    // The map while it is up or closing (see openMap): its centre, the
    // windows in it (others fade out), where piled windows spread to
    // (see spreadPiles), when it opened, and when it started closing and
    // how far open it was then. `chosen` stays undimmed while it closes.
    struct Map
    {
        QPointF center;
        std::set<Window *> windows = {};
        std::map<Window *, QRectF> spread = {};
        std::chrono::steady_clock::time_point opened = {};
        bool closing = false;
        std::chrono::steady_clock::time_point closed = {};
        qreal openAtClose = 0;
        QPointer<Window> chosen = nullptr;
    };
    std::optional<Map> m_map;
    // How far open the map is in this frame (0 to 1).
    qreal m_mapOpen = 0;
    // The label's texture, its size on screen, and what it shows.
    std::unique_ptr<GLTexture> m_label;
    QSizeF m_labelSize;
    QPointer<Window> m_labelWindow;
    QString m_labelCaption;
    qreal m_labelScale = 0;

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
        if (!std::ranges::contains(m_recent, window)) {
            m_recent.push_back(window); // new, not used yet
        }
        connect(window, &Window::interactiveMoveResizeStepped, this, [this, window]() {
            dragStep(window);
        });
        connect(window, &Window::interactiveMoveResizeFinished, this, [this, window]() {
            dragFinished(window);
        });
        connect(window, &Window::frameGeometryChanged, this, [this, window]() {
            clipHeightChanged(window);
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
            if (window == highlighted()) {
                updateRing();
            }
        });
        connect(window, &Window::closed, this, [this, window]() {
            if (window == m_ringWindow) {
                removeRing(); // while its parent item still exists
            }
            std::erase(m_recent, window);
            if (m_clipDrag && m_clipDrag->window == window) {
                m_clipDrag.reset(); // pasted: the clip is gone
                effects->addRepaintFull();
            }
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
        Window *window = pick(event->position);
        // Clips get their presses: dragging a clip drags its text, and an
        // app can only start a drag from a press it received.
        if (!window || isClip(window) || !isIcon(window)) {
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
        fillHalf(window, half);
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
            && (target ? m_declutter->target == target && m_parking.placeOf(target) == m_declutter->half : m_declutter->desktop);
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
            auto *it = m_parking.find(window);
            const bool parked = it && !it->restoring;
            saved.saved.push_back(Saved{.window = window,
                                        .parked = parked ? std::optional<Parked>(*it) : std::nullopt,
                                        .frame = window->moveResizeGeometry(),
                                        .maximize = window->maximizeMode()});
            if (window == target) {
                continue;
            }
            if (!parked) {
                movers.push_back(window);
            } else if (const int area = m_parking.areaOf(window); area == 1 || area == 3) {
                stashed[area == 1 ? 0 : 1].push_back(window);
            }
        }

        // Balance the stashes: the leftmost `toLeft` movers go left, the rest
        // right, so both end up with about as many windows.
        std::sort(movers.begin(), movers.end(), [this](Window *a, Window *b) {
            return m_parking.currentlyDrawn(a).center().x() < m_parking.currentlyDrawn(b).center().x();
        });
        const int count = int(movers.size());
        const int toLeft = std::clamp(int(std::lround((count + int(stashed[1].size()) - int(stashed[0].size())) / 2.0)), 0, count);
        stashed[0].insert(stashed[0].end(), movers.begin(), movers.begin() + toLeft);
        stashed[1].insert(stashed[1].end(), movers.begin() + toLeft, movers.end());

        if (target) {
            saved.half = m_parking.currentlyDrawn(target).center().x() < middle ? Place::HalfLeft : Place::HalfRight;
            fillHalf(target, saved.half);
        }
        // Each stash at one scale, so its column lines up.
        for (int side = 0; side < 2; ++side) {
            if (movers.empty() || stashed[side].empty()) {
                continue;
            }
            const Place stash = side == 0 ? Place::StashLeft : Place::StashRight;
            const qreal scale = m_parking.fittingScale(output, stashed[side]);
            for (Window *window : stashed[side]) {
                m_parking.moveTo(window, stash, scale);
            }
            m_parking.arrangeArea(side == 0 ? 1 : 3, output, nullptr);
        }
        if (target) {
            workspace()->activateWindow(target);
        }
        m_declutter = std::move(saved);
    }

    // `window` into `half` of main at the full usable height, gliding.
    void fillHalf(Window *window, Place half)
    {
        releaseKdeState(window);
        const QRectF from = m_parking.currentlyDrawn(window);
        const auto closeRanks = m_parking.leaving(window);
        const RectF screen = window->output()->geometryF();
        const RectF area = workspace()->clientArea(MaximizeArea, window);
        const qreal zoneWidth = screen.width() * zoneFraction;
        const qreal x = screen.x() + (half == Place::HalfLeft ? zoneWidth : 2 * zoneWidth);
        m_parking.resizeAnimated(window, RectF(x, area.y(), zoneWidth, area.height()), from);
        closeRanks();
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
            const QRectF from = m_parking.currentlyDrawn(window);
            if (saved.parked) {
                m_parking.park(window, saved.parked->shown, saved.parked->original);
                m_parking.animate(window, from);
            } else if (saved.maximize != MaximizeRestore) {
                m_parking.erase(window);
                m_parking.setDrawTransform(window, QTransform());
                window->maximize(saved.maximize);
            } else if (m_parking.isParked(window) || window->moveResizeGeometry() != saved.frame) {
                m_parking.resizeAnimated(window, saved.frame, from);
            }
        }
        if (declutter.target) {
            workspace()->activateWindow(declutter.target);
        }
    }

    // --- Hover previews ---

    // A parked window that acts like an icon: anything in parking, and a
    // stashed one shown small enough (see iconBelow). Narrow windows (clips,
    // Firefox at its 500 px minimum) are drawn above iconBelow in parking
    // (see parkingScale) but are icons there all the same.
    bool isIcon(Window *window) const
    {
        const Parked *parked = m_parking.find(window);
        return parked && !parked->restoring
            && (parked->shown.width() / parked->original.width() < iconBelow
                || m_parking.isParkingArea(m_parking.areaOf(window)));
    }

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
        return isIcon(window) && m_parking.isParkingArea(m_parking.areaOf(window));
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
        Window *window = pick(event->position);
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

    // --- Clips: text dropped on the desktop ---

    // A text or image drag released over the desktop: rather than letting
    // Plasma make a sticky-note widget of it, ask the dragging app for the
    // data and hold the release back. Once it is in (finishClip), the drag
    // is cancelled, so nothing is dropped anywhere, and the release passed
    // on. Returns whether the release was taken. Dragged files: a single
    // image file becomes a clip, anything else is dropped on Plasma after
    // all; links without image data are left to Plasma.
    bool dropToClip(PointerButtonEvent *event)
    {
        auto seat = waylandServer()->seat();
        if (m_clip || event->button != Qt::LeftButton || !seat->isDragPointer() || !seat->dragSource()) {
            return false;
        }
        // (A dragged clip is drawn under the pointer, not where it was.)
        Window *under = pick(event->position, m_clipDrag ? m_clipDrag->window.data() : nullptr);
        if (under && !under->isDesktop() && !(isIcon(under) && parkingSide(event->position))) {
            return false;
        }
        const QStringList types = seat->dragSource()->mimeTypes();
        const QString mimeType = clipMimeType(types, true);
        if (!m_clipDrag) {
            qInfo("glance: clip: drop offers %s", qPrintable(types.join(QLatin1Char(' '))));
        }
        if (mimeType.isEmpty()) {
            return false;
        }
        if (m_clipDrag) {
            // A clip dropped where text would become a new clip: it moves
            // there instead (see placeDroppedClip). The drag is cancelled,
            // so Plasma makes no note of it.
            placeDroppedClip(event->position);
            seat->cancelDrag();
            seat->setTimestamp(event->timestamp);
            seat->notifyPointerButton(event->nativeButton, event->state);
            seat->notifyPointerFrame();
            return true;
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
        const QString mimeType = clipMimeType(source->mimeTypes(), false);
        if (mimeType.isEmpty()) {
            return;
        }
        const QRectF drawn = m_parking.currentlyDrawn(window);
        const RectF screen = window->output()->geometryF();
        const bool left = drawn.center().x() < screen.x() + screen.width() / 2;
        startClip(source, mimeType,
                  ClipRead{.position = drawn.center(), .place = left ? Place::ParkingLeft : Place::ParkingRight});
    }

    // What to ask for of `types` to make a clip, or empty: with `images`,
    // image data first (an image dragged out of a browser comes with its
    // link too), then a file list (finishClip takes it only if it is one
    // image file); then plain text, unless it comes with a file list or
    // link (text/uri-list).
    static QString clipMimeType(const QStringList &types, bool images)
    {
        if (images) {
            if (types.contains(QStringLiteral("image/png"))) {
                return QStringLiteral("image/png");
            }
            const QList<QByteArray> readable = QImageReader::supportedMimeTypes();
            for (const QString &type : types) {
                if (type.startsWith(QLatin1String("image/")) && readable.contains(type.toLatin1())) {
                    return type;
                }
            }
        }
        if (types.contains(QStringLiteral("text/uri-list"))) {
            return images ? QStringLiteral("text/uri-list") : QString();
        }
        for (const char *type : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"}) {
            if (types.contains(QLatin1String(type))) {
                return QLatin1String(type);
            }
        }
        return {};
    }

    // Ask `source` for its text or image; it arrives in readClip, and
    // finishClip makes the clip. Returns whether it started.
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
        clip.mimeType = mimeType;
        clip.notifier = std::make_unique<QSocketNotifier>(fds[0], QSocketNotifier::Read);
        m_clip = std::make_unique<ClipRead>(std::move(clip));
        connect(m_clip->notifier.get(), &QSocketNotifier::activated, this, &Glance::readClip);
        // Images take longer: the app may encode them first.
        const auto timeout = mimeType.startsWith(QLatin1String("image/")) ? clipImageTimeout : clipTimeout;
        QTimer::singleShot(timeout, this, [this, fd = fds[0]]() {
            if (m_clip && m_clip->fd == fd) {
                qWarning("glance: clip: the app took too long to hand over the data");
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

    // All data read (or given up): for a drop, end the drag and pass the
    // release on; save and open the clip. A dragged file that isn't a
    // single image is dropped where it was going after all (on Plasma).
    void finishClip()
    {
        const std::unique_ptr<ClipRead> clip = std::move(m_clip);
        // We may be inside the notifier's own signal: delete it later.
        clip->notifier->setEnabled(false);
        clip->notifier.release()->deleteLater();
        close(clip->fd);

        QString imageFile;
        if (clip->mimeType == QLatin1String("text/uri-list")) {
            imageFile = droppedImageFile(clip->text);
        }
        const bool image = clip->mimeType.startsWith(QLatin1String("image/"));
        const bool ours = image || !imageFile.isEmpty() || clip->mimeType != QLatin1String("text/uri-list");

        if (clip->fromDrag) {
            auto seat = waylandServer()->seat();
            if (ours) {
                seat->cancelDrag();
            }
            seat->setTimestamp(clip->timestamp);
            seat->notifyPointerButton(clip->nativeButton, PointerButtonState::Released);
            seat->notifyPointerFrame();
        }
        if (!ours) {
            return;
        }

        if (image && QImage::fromData(clip->text).isNull()) {
            qWarning("glance: clip: no image received (%s, %lld bytes)", qPrintable(clip->mimeType), qlonglong(clip->text.size()));
            return;
        }
        if (!image && imageFile.isEmpty() && clip->text.trimmed().isEmpty()) {
            qWarning("glance: clip: no text received");
            return;
        }
        const QDir dir(QDir::home().filePath(QLatin1String(clipsFolder)));
        if (!dir.mkpath(QStringLiteral("."))) {
            qWarning("glance: clip: can't create %s", qPrintable(dir.path()));
            return;
        }
        QString suffix = QStringLiteral("txt");
        if (image) {
            suffix = QMimeDatabase().mimeTypeForName(clip->mimeType).preferredSuffix();
        } else if (!imageFile.isEmpty()) {
            suffix = QFileInfo(imageFile).suffix().toLower();
        }
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
        QString path = dir.filePath(stamp + QLatin1Char('.') + suffix);
        for (int i = 2; QFile::exists(path); ++i) {
            path = dir.filePath(QStringLiteral("%1 (%2).%3").arg(stamp).arg(i).arg(suffix));
        }
        // A dropped image file is copied: closing the clip deletes its file.
        if (!imageFile.isEmpty()) {
            if (!QFile::copy(imageFile, path)) {
                qWarning("glance: clip: can't copy %s to %s", qPrintable(imageFile), qPrintable(path));
                return;
            }
        } else {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(clip->text) != clip->text.size()) {
                qWarning("glance: clip: can't write %s", qPrintable(path));
                return;
            }
            file.close();
        }

        // KWin's own environment for the apps it starts, without the plugin
        // path that loads this effect from the build folder. Started in its
        // own systemd scope in app.slice, like apps Plasma starts: otherwise
        // it would belong to KWin's service, be stopped in an odd order at
        // logout and die with KWin. systemd-run --scope execs the app in its
        // own process, so the pid is the app's (see placeClip).
        QProcessEnvironment env = kwinApp()->processStartupEnvironment();
        env.remove(QStringLiteral("QT_PLUGIN_PATH"));
        const QString unit = QStringLiteral("app-%1-%2.scope").arg(QLatin1String(clipAppId)).arg(QDateTime::currentMSecsSinceEpoch());
        QProcess process;
        process.setProgram(QStringLiteral("systemd-run"));
        process.setArguments({QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--slice=app.slice"),
                              QStringLiteral("--unit=") + unit, QStringLiteral("--collect"), QStringLiteral("--quiet"),
                              QStringLiteral("--"), clipApp(), path});
        process.setProcessEnvironment(env);
        qint64 pid = 0;
        if (!process.startDetached(&pid)) {
            qWarning("glance: clip: can't start %s", qPrintable(clipApp()));
            return;
        }
        m_clipPid = pid;
        m_clipPosition = clip->position;
        m_clipPlace = clip->place;
        qInfo("glance: clip: %s -> %s", qPrintable(imageFile.isEmpty() ? QStringLiteral("%1 bytes of %2").arg(clip->text.size()).arg(clip->mimeType) : imageFile),
              qPrintable(path));
    }

    // The local image file a dropped file list (text/uri-list: one URL per
    // line, # comments) holds, if it holds just one, else empty.
    static QString droppedImageFile(const QByteArray &uriList)
    {
        QStringList files;
        for (const QByteArray &line : uriList.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('#')) {
                continue;
            }
            const QUrl url(QString::fromUtf8(trimmed));
            if (!url.isLocalFile()) {
                return {};
            }
            files.append(url.toLocalFile());
        }
        if (files.size() != 1 || !QImageReader(files.first()).canRead()) {
            return {};
        }
        return files.first();
    }

    // The clip app: the one built with this plugin (bin/glance-clip in the
    // build folder, three levels above the plugin's bin/kwin/effects/plugins),
    // else the installed one, from PATH.
    static QString clipApp()
    {
        Dl_info info;
        if (dladdr(reinterpret_cast<void *>(&Glance::clipApp), &info) && info.dli_fname) {
            const QString besidePlugin = QFileInfo(QFile::decodeName(info.dli_fname)).dir().filePath(QStringLiteral("../../../glance-clip"));
            if (QFileInfo(besidePlugin).isExecutable()) {
                return QFileInfo(besidePlugin).canonicalFilePath();
            }
        }
        return QStringLiteral("glance-clip");
    }

    // A clip in parking resizes itself to the height its text needs (see
    // resizeEvent in kwin/clip/main.cpp): take it (drawn at 1/2, see
    // layoutSize) and re-form its column.
    void clipHeightChanged(Window *window)
    {
        auto *it = m_parking.find(window);
        if (!it || it->restoring
            || !isClipInParking(window, it->shown.width(), it->original)) {
            return;
        }
        const RectF frame = window->frameGeometry();
        const qreal scale = it->shown.width() / frame.width();
        const qreal height = frame.height() * scale;
        if (std::abs(height - it->shown.height()) > 0.5) {
            it->shown.setHeight(height);
            m_parking.arrange(window);
        }
    }

    // The clip window a drag comes from, if any.
    static Window *clipWindowOf(AbstractDataSource *source)
    {
        if (!source) {
            return nullptr;
        }
        for (Window *window : workspace()->windows()) {
            if (isClip(window) && window->surface() && window->surface()->client()->client() == source->client()) {
                return window;
            }
        }
        return nullptr;
    }

    // --- Clips: dragging a clip ---
    //
    // Dragging a clip's body is a real drag and drop of its text (only then
    // can an app say it takes text), but it looks like moving the note:
    // Glance draws the clip under the pointer, by the edge rule like a
    // moved window, and the app shows no drag picture. Where it lands
    // decides: an app that takes it gets it pasted, and the clip is gone
    // (the app closes itself; with Shift it is copied and the clip comes
    // back); the desktop or nothing, the clip moves there; anything else,
    // it slides back.

    void clipDragStarted()
    {
        Window *window = clipWindowOf(waylandServer()->seat()->dragSource());
        if (!window || !window->windowItem()) {
            return;
        }
        if (m_parking.isParked(window)) {
            m_parking.at(window).preview.reset();
            if (m_preview == window) {
                m_preview = nullptr;
            }
        }
        const QRectF drawn = m_parking.currentlyDrawn(window);
        auto *it = m_parking.find(window);
        const RectF frame = window->frameGeometry();
        ClipDrag drag;
        drag.window = window;
        drag.grab = QPointF((m_lastPress.x() - drawn.x()) / drawn.width(), (m_lastPress.y() - drawn.y()) / drawn.height());
        drag.original = it ? it->original : QSizeF(frame.width(), frame.height());
        drag.aspect = drawn.height() / drawn.width();
        drag.ghost = drawn;
        m_clipDrag = drag;
        m_clipDrag->ghost = clipGhostRect(input()->pointer()->pos());
        workspace()->raiseWindow(window);
        effects->addRepaintFull();
        qInfo("glance: clip drag started");
    }

    // Where a dragged clip is drawn with the pointer at `cursor`: held at
    // the spot it was grabbed, scaled by the edge rule like a moved window
    // (full size in main, down to parking size at the edges), in the shape
    // it had when the drag started.
    QRectF clipGhostRect(const QPointF &cursor) const
    {
        const ClipDrag &drag = *m_clipDrag;
        LogicalOutput *output = workspace()->outputAt(cursor);
        const RectF screen = output ? output->geometryF() : RectF();
        const qreal zoneWidth = screen.width() * zoneFraction;
        const qreal width = drag.original.width();
        const qreal scale = std::clamp(std::min({edgeScale(cursor.x() - screen.x(), drag.grab.x() * width, zoneWidth),
                                                 edgeScale(screen.x() + screen.width() - cursor.x(), (1 - drag.grab.x()) * width, zoneWidth)}),
                                       parkingScale(drag.original), 1.0);
        const QSizeF size(width * scale, width * scale * drag.aspect);
        const QPointF topLeft = cursor - QPointF(drag.grab.x() * size.width(), drag.grab.y() * size.height());
        // Kept on the screen, like a moved window.
        return QRectF(QPointF(topLeft.x() + shiftOntoScreen(topLeft.x(), size.width(), screen), topLeft.y()), size);
    }

    // A dragged clip dropped at `pos` on the desktop (or nothing, or the
    // parking band). In the parking band it joins that parking column,
    // against the screen edge (as text dropped there does, see
    // parkingSide); elsewhere it stays where and as large as it is drawn,
    // like a moved window dropped there (parked if shrunk).
    void placeDroppedClip(const QPointF &pos)
    {
        const ClipDrag drag = *m_clipDrag;
        m_clipDrag.reset();
        Window *window = drag.window;
        if (!window || !window->windowItem()) {
            return;
        }
        const QRectF from = drag.ghost;
        const auto closeRanks = m_parking.leaving(window);
        const qreal scale = drag.ghost.width() / drag.original.width();
        if (const std::optional<Place> parking = parkingSide(pos)) {
            m_parking.commitPlace(window, *parking, drag.original, drag.ghost.center().y(), from);
        } else if (scale < parkBelow) {
            m_parking.park(window, QRectF(drag.ghost.topLeft(), drag.original * scale), drag.original);
            m_parking.animate(window, from);
            m_parking.arrange(window);
        } else {
            m_parking.resizeAnimated(window, RectF(drag.ghost.x(), drag.ghost.y(), drag.original.width(), drag.original.height()), from);
        }
        closeRanks();
        qInfo("glance: clip dropped on the desktop: moved there");
    }

    // The drag ended. Pasted (moved): the app closes the clip; it stays
    // drawn where it was dropped until then (see Window::closed in watch),
    // or slides back if it doesn't. Copied, or not taken: it slides back.
    void clipDragEnded()
    {
        if (!m_clipDrag) {
            return;
        }
        if (m_clipDrag->dropped && !m_clipDrag->copy) {
            qInfo("glance: clip pasted (moved)");
            QTimer::singleShot(clipCloseWait, this, [this, window = m_clipDrag->window]() {
                if (m_clipDrag && m_clipDrag->window == window) {
                    clipSlideBack();
                }
            });
            return;
        }
        qInfo(m_clipDrag->dropped ? "glance: clip pasted (copied): back to its place" : "glance: clip not taken: back to its place");
        clipSlideBack();
    }

    void clipSlideBack()
    {
        const ClipDrag drag = *m_clipDrag;
        m_clipDrag.reset();
        Window *window = drag.window;
        if (!window || !window->windowItem()) {
            return;
        }
        if (m_parking.isParked(window)) {
            m_parking.animate(window, drag.ghost);
        } else {
            // Not parked: glide from where it was dropped to its frame.
            m_parking.resizeAnimated(window, window->frameGeometry(), drag.ghost);
        }
        effects->addRepaintFull();
    }

    // ParkingLeft/Right if `pos` is in that side's parking band (see
    // parkingBand).
    std::optional<Place> parkingSide(const QPointF &pos) const
    {
        LogicalOutput *output = workspace()->outputAt(pos);
        if (!output) {
            return std::nullopt;
        }
        const RectF screen = output->geometryF();
        const qreal band = screen.width() * zoneFraction * parkingBand;
        if (pos.x() - screen.x() < band) {
            return Place::ParkingLeft;
        }
        if (screen.x() + screen.width() - pos.x() < band) {
            return Place::ParkingRight;
        }
        return std::nullopt;
    }

    // The clip's window appeared: put it where the text was dropped,
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
            m_parking.commitPlace(window, *m_clipPlace, size, pos.y(), m_parking.currentlyDrawn(window));
            workspace()->activateWindow(window);
            return;
        }
        const qreal zoneWidth = screen.width() * zoneFraction;
        const qreal scale = std::max(parkingScale(size), std::min({1.0,
                                                         edgeScale(pos.x() - screen.x(), size.width() / 2, zoneWidth),
                                                         edgeScale(screen.x() + screen.width() - pos.x(), size.width() / 2, zoneWidth)}));
        const QSizeF drawn = size * scale;
        const qreal left = pos.x() - drawn.width() / 2;
        const qreal x = left + shiftOntoScreen(left, drawn.width(), screen);
        const qreal y = std::clamp(pos.y() - drawn.height() / 2, area.y(), std::max(area.y(), area.y() + area.height() - drawn.height()));
        if (scale < parkBelow) {
            const QRectF from = m_parking.currentlyDrawn(window);
            m_parking.park(window, QRectF(QPointF(x, y), drawn), size);
            m_parking.animate(window, from);
            m_parking.arrange(window);
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
        const QPointF from = active ? m_parking.currentlyDrawn(active).center() : input()->pointer()->pos();
        Window *best = nullptr;
        qreal bestScore = 0;
        for (Window *window : workspace()->stackingOrder()) {
            if (window == active || !switchable(window)) {
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

    // A window one can select (Meta+Alt+arrows, Alt+Tab): one that is shown
    // on the screen (not e.g. KDE's hidden Xwayland Video Bridge, which then
    // can't be activated and blocks the way).
    bool switchable(Window *window) const
    {
        if (window->isDeleted() || !window->wantsTabFocus() || window->skipSwitcher() || window->isMinimized()
            || !window->isShown() || window->isHiddenByShowDesktop() || !window->readyForPainting()
            || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
            return false;
        }
        const RectF screen = window->output()->geometryF();
        return m_parking.currentlyDrawn(window).intersects(QRectF(screen.x(), screen.y(), screen.width(), screen.height()));
    }

    // --- Alt+Tab: hunt and return ---

    void noteActivated(Window *window)
    {
        if (window) {
            std::erase(m_recent, window);
            m_recent.insert(m_recent.begin(), window);
        }
    }

    // Alt+Tab or Meta+Tab (also with Shift) starts a switch.
    bool startsSwitch(const KeyboardKeyEvent *event) const
    {
        const Qt::KeyboardModifiers modifiers = event->modifiers & ~Qt::ShiftModifier;
        return event->state == KeyboardKeyState::Pressed && (event->key == Qt::Key_Tab || event->key == Qt::Key_Backtab)
            && (modifiers == Qt::AltModifier || modifiers == Qt::MetaModifier) && !workspace()->moveResizeWindow()
            && !m_pending;
    }

    // Every key while a switch is on: Tab / Shift+Tab (Backtab) step through
    // the windows, Esc cancels, releasing the modifier chooses. Other keys do
    // nothing (no window has the keyboard meanwhile, see startSwitch).
    // Returns whether to swallow the key: under Alt, Tab and Esc are ours.
    // Under Meta they are passed on, as for Meta+arrows (see onKey): KDE's
    // own switcher actions on them are disabled.
    bool switchKey(KeyboardKeyEvent *event)
    {
        if (!m_switch) {
            startSwitch(event->modifiers & Qt::AltModifier ? Qt::AltModifier : Qt::MetaModifier);
        }
        if (!(event->modifiers & m_switch->modifier)) {
            finishSwitch(true);
            return false; // the modifier's release goes on
        }
        const bool tab = event->key == Qt::Key_Tab || event->key == Qt::Key_Backtab;
        const bool escape = event->key == Qt::Key_Escape;
        const bool swallow = (tab || escape) && m_switch->modifier == Qt::AltModifier;
        if (swallow && event->state == KeyboardKeyState::Pressed) {
            input()->keyboard()->addFilteredKey(event->nativeScanCode); // its release isn't sent either
        }
        if (event->state != KeyboardKeyState::Released) {
            if (tab) {
                step(event->key == Qt::Key_Backtab || (event->modifiers & Qt::ShiftModifier) ? -1 : 1);
            } else if (escape) {
                finishSwitch(false);
            }
        }
        return swallow;
    }

    void startSwitch(Qt::KeyboardModifier modifier)
    {
        Switch s;
        s.modifier = modifier;
        for (Window *window : m_recent) {
            if (switchable(window)) {
                s.windows.push_back(window);
            }
        }
        // The first Tab goes to the second window, the one used before the
        // active one; if no switchable window is active, to the first.
        s.index = !s.windows.empty() && s.windows.front() == workspace()->activeWindow() ? 0 : -1;
        m_switch = std::move(s);
        // Like KDE's own switcher: no window has the keyboard meanwhile, so
        // the app doesn't get the keys, nor a lone Alt press and release
        // (Firefox would show its menu bar).
        waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
        m_hold.start();
        qInfo("glance: switch started (%d windows)", int(m_switch->windows.size()));
    }

    void step(int direction)
    {
        Switch &s = *m_switch;
        const int n = int(s.windows.size());
        for (int tries = 0; tries < n; ++tries) {
            s.index = s.index < 0 ? (direction > 0 ? 0 : n - 1) : (s.index + direction + n) % n;
            if (s.windows[s.index]) {
                break; // skips windows closed meanwhile
            }
        }
        bounceRing(mapSelected()); // the ring (and bounce) go to the selection
        effects->addRepaintFull();
    }

    // The selected window: the switch's, or the chosen one while the map
    // closes.
    Window *mapSelected() const
    {
        if (m_switch) {
            return m_switch->index >= 0 ? m_switch->windows[m_switch->index].data() : nullptr;
        }
        return m_map ? m_map->chosen.data() : nullptr;
    }

    // End the switch: activate the selected window (`accept`), or leave
    // things as they were.
    void finishSwitch(bool accept)
    {
        QPointer<Window> chosen = accept ? mapSelected() : nullptr;
        const bool mapped = m_map && !m_map->closing;
        m_switch.reset();
        m_chosen = chosen; // keeps the ring until it is active
        m_hold.stop();
        if (m_map) {
            closeMap(chosen);
        }
        if (chosen) {
            qInfo("glance: window chosen: %s", qPrintable(chosen->caption()));
        } else {
            qInfo("glance: switch cancelled");
        }
        // Once the key event that ended the switch has gone through (with no
        // keyboard focus, so the app doesn't see the modifier's release),
        // give the keyboard back. The chosen window has the ring already;
        // after the map it bounces again as it gets focus. Cancelled, the
        // ring goes back to the active window.
        QTimer::singleShot(0, this, [this, chosen, mapped]() {
            m_chosen = nullptr;
            if (chosen && !chosen->isDeleted() && chosen != workspace()->activeWindow()) {
                workspace()->activateWindow(chosen);
            }
            updateRing();
            if (chosen && mapped && chosen == m_ringWindow) {
                startBounce(chosen);
            }
            input()->keyboard()->update();
        });
    }

    // --- Alt+Tab: the map ---

    bool inMap(Window *window) const
    {
        return window->isDesktop() || m_map->windows.contains(window);
    }

    void openMap()
    {
        const RectF screen = workspace()->activeOutput()->geometryF();
        m_map = Map{};
        m_map->center = QPointF(screen.x() + screen.width() / 2, screen.y() + screen.height() / 2);
        for (const QPointer<Window> &window : m_switch->windows) {
            if (window) {
                m_map->windows.insert(window);
            }
        }
        m_map->opened = std::chrono::steady_clock::now();
        m_map->spread = spreadPiles(QRectF(screen.x(), screen.y(), screen.width(), screen.height()));
        effects->addRepaintFull();
        qInfo("glance: map shown (%d windows, %d spread)", int(m_map->windows.size()), int(m_map->spread.size()));
    }

    // Start zooming the map back to full size from wherever it is now.
    void closeMap(Window *chosen)
    {
        m_map->openAtClose = mapProgress();
        m_map->closing = true;
        m_map->closed = std::chrono::steady_clock::now();
        m_map->chosen = chosen;
        effects->addRepaintFull();
    }

    // How far open the map is (0 to 1, eased): opening or closing takes
    // mapTime.
    qreal mapProgress() const
    {
        const auto ease = [](qreal t) {
            return 1.0 - std::pow(1.0 - std::clamp(t, 0.0, 1.0), 3);
        };
        const auto now = std::chrono::steady_clock::now();
        if (m_map->closing) {
            return m_map->openAtClose * (1.0 - ease(std::chrono::duration<qreal>(now - m_map->closed) / mapTime));
        }
        return ease(std::chrono::duration<qreal>(now - m_map->opened) / mapTime);
    }

    // Where the map draws something drawn at `rect`: scaled by mapScale
    // toward the screen's centre.
    QRectF toMap(const QRectF &rect) const
    {
        const QPointF c = m_map->center;
        return QRectF(c.x() + (rect.x() - c.x()) * mapScale, c.y() + (rect.y() - c.y()) * mapScale,
                      rect.width() * mapScale, rect.height() * mapScale);
    }

    // Where a window drawn at `from` is drawn in this frame of the map: on
    // the straight way to its place there (shrinking and spreading at once).
    QRectF mapped(Window *window, const QRectF &from) const
    {
        auto it = m_map->spread.find(window);
        return lerpRect(from, it != m_map->spread.end() ? it->second : toMap(from), m_mapOpen);
    }

    // Piles in the map (see glance::spreadPiles), spread so every window
    // can be counted and pointed at. Parked windows stay out: their columns
    // and slight stash overlaps don't count. Returns where they go (global,
    // at map scale).
    std::map<Window *, QRectF> spreadPiles(const QRectF &screen) const
    {
        // Free windows in the map, front first, where the map draws them.
        std::vector<std::pair<Window *, QRectF>> items;
        const auto &stacking = workspace()->stackingOrder();
        for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
            if (m_map->windows.contains(*it) && !m_parking.isParked(*it)) {
                items.emplace_back(*it, toMap(m_parking.currentlyDrawn(*it)));
            }
        }
        std::vector<QRectF> rects;
        for (const auto &item : items) {
            rects.push_back(item.second);
        }
        const auto places = glance::spreadPiles(rects, screen);
        std::map<Window *, QRectF> spread;
        for (size_t i = 0; i < items.size(); ++i) {
            if (places[i]) {
                spread[items[i].first] = *places[i];
            }
        }
        return spread;
    }

    // --- Alt+Tab: the label ---

    // The selected window's icon and title on one line, on a rounded
    // translucent card, centred on the window where the map draws it, its
    // bottom on the window's bottom edge: always in the same spot, inside
    // the window (wider than the window if need be). Fades with the map.
    // Redrawn when the selection (or its title) changes.
    void paintLabel(const RenderTarget &renderTarget, const RenderViewport &viewport, LogicalOutput *screen)
    {
        Window *window = mapSelected();
        if (!window) {
            return;
        }
        const qreal scale = viewport.scale();
        if (!m_label || m_labelWindow != window || m_labelCaption != window->caption() || m_labelScale != scale) {
            const QImage image = labelImage(window, scale);
            m_label = GLTexture::upload(image);
            if (!m_label) {
                return;
            }
            m_label->setFilter(GL_LINEAR);
            m_labelSize = QSizeF(image.size()) / scale;
            m_labelWindow = window;
            m_labelCaption = window->caption();
            m_labelScale = scale;
        }
        const RectF area = screen->geometryF();
        const QRectF drawn = inMap(window) ? mapped(window, m_parking.currentlyDrawn(window)) : m_parking.currentlyDrawn(window);
        const QSizeF size = m_labelSize;
        const qreal x = std::clamp(drawn.center().x() - size.width() / 2, area.left(), area.right() - size.width());
        const QPointF topLeft(x, drawn.bottom() - size.height());
        QMatrix4x4 mvp = viewport.projectionMatrix();
        mvp.translate(std::round(topLeft.x() * scale), std::round(topLeft.y() * scale));
        const qreal opacity = m_mapOpen;

        GLShader *shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::Modulate
                                                                 | ShaderTrait::TransformColorspace);
        shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
        shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(opacity, opacity, opacity, opacity));
        shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied
        m_label->render(QSizeF(m_label->size()));
        glDisable(GL_BLEND);
        ShaderManager::instance()->popShader();
    }

    static QImage labelImage(Window *window, qreal devicePixelRatio)
    {
        QFont font = QGuiApplication::font();
        font.setPixelSize(labelTextSize);
        const QFontMetricsF metrics(font);
        const QString title = metrics.elidedText(window->caption(), Qt::ElideRight, labelMaxWidth);
        const qreal textWidth = metrics.horizontalAdvance(title);
        const QSizeF size(2 * labelPadding + labelIconSize + labelGap + textWidth,
                          2 * labelPadding + std::max(labelIconSize, metrics.height()));

        QImage image((size * devicePixelRatio).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 200));
        painter.drawRoundedRect(QRectF(QPointF(0, 0), size), labelRadius, labelRadius);
        QIcon icon = window->icon();
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }
        icon.paint(&painter, QRectF(labelPadding, (size.height() - labelIconSize) / 2, labelIconSize, labelIconSize).toRect());
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(labelPadding + labelIconSize + labelGap, 0, textWidth + 1, size.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, title);
        return image;
    }

    // The highlighted window, which gets the focus ring: the active one, or
    // during Alt+Tab the selected one (then the chosen one until it is
    // active).
    Window *highlighted() const
    {
        if (m_switch) {
            return mapSelected();
        }
        return m_chosen ? m_chosen.data() : workspace()->activeWindow();
    }

    // Outline the highlighted window: a line in the accent color just outside
    // its frame, ringWidth wide on screen whatever the window's scale (also
    // in the Alt+Tab map). It is a child of the window's scene item (whose
    // coordinates start at the frame's top-left corner), so it moves, scales
    // and stacks with it.
    void updateRing()
    {
        Window *window = highlighted();
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
        qreal scale = window->windowItem()->transform().m11();
        if (m_map && inMap(window)) {
            const QRectF drawn = m_parking.currentlyDrawn(window);
            if (drawn.width() > 0) {
                scale *= mapped(window, drawn).width() / drawn.width();
            }
        }
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

    // The window really visible at `pos`, like InputRedirection::findToplevel
    // but using the drawn rectangle for parked windows.
    Window *pick(const QPointF &pos, Window *ignore = nullptr) const
    {
        const auto &stacking = workspace()->stackingOrder();
        for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
            Window *window = *it;
            if (window == ignore || window->isDeleted() || !window->isOnCurrentActivity() || !window->isOnCurrentDesktop()
                || window->isMinimized() || window->isHidden() || window->isHiddenByShowDesktop()
                || !window->readyForPainting()) {
                continue;
            }

            if (m_parking.isParked(window)) {
                if (m_parking.drawnRect(window).contains(pos)) {
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

        Window *target = pick(pos);
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
