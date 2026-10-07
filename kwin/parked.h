// Parked windows: the windows Glance draws somewhere other than their real
// frame (in a stash, in parking, or gliding back to full size), their
// places, animation and making room, and how every window is drawn.
//
// A parked window has a real frame (what the app and KWin's input know,
// in parking resized to a small layout size, see layoutSize) and a drawn
// rectangle (`shown`); a transform on its scene item fits the one into the other
// (applyParked). Keeping the two in step is most of Glance.
//
// Dropped while shrunk, a window stays exactly where and as large as it was
// drawn. In parking the app is really resized, to a phone-like width, so
// web pages reflow; in a stash it keeps its full size. The rest of the
// shrink is the transform, which fits the
// window's current frame into `shown` also while the app is still catching
// up with the new size. Back in main, a window gets its original size.
//
// Columns: the windows in each parking area form one column, centered
// vertically, in the order of their vertical position (see arrangeArea).
// Whenever a window arrives (keyboard or drop) or leaves (keyboard, dragged
// out, closed), the column re-forms, animated. Stashes have no column:
// windows stay where they were put and may overlap (a keyboard move keeps
// the window's height); only declutter lines a stash up. Crowding of
// parking (a column taller than the screen) comes later.
//
// Parking icons are tiles (2026-10-06): square apps (see layoutSize),
// all drawn parkingTile large against the screen edge, and turned away
// from the viewer around their outer edge ("tilt", see updateTilt). The
// tilt is drawn only where the window is drawn from mipmaps (see
// Mipmaps; tilted windows always are) and is taken into account by input
// (pick, untilt); the draw transform stays a plain scale and move, which
// all of Glance and KWin's repainting rely on (the tilted tile lies inside
// the flat one).
//
// Minimize = park: parking is Glance's minimize (see minimizeToParking).
#pragma once

#include "tuning.h"

#include <core/rect.h>

#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace KWin
{
class LogicalOutput;
class Window;
class WindowPaintData;
}

namespace glance
{

using KWin::LogicalOutput;
using KWin::RectF;
using KWin::Window;

// A clip window (glance-clip's app id, see docs/clips.md).
bool isClip(Window *window);
// A clip shown `shownWidth` wide in parking (by its scale).
bool isClipInParking(Window *window, qreal shownWidth, const QSizeF &original);
// KDE's own maximized or tiled state would fight our geometry.
void releaseKdeState(Window *window);
// A window Glance may put somewhere else (declutter, Meta+wheel, the
// keyboard moves).
bool manageable(Window *window);
// Change paint data so the window drawn at `from` is drawn at `to` (same
// shape), on top of whatever the data does already (the Alt+Tab map, a
// dragged clip).
void retarget(KWin::WindowPaintData &data, Window *window, const QRectF &from, const QRectF &to);

class ParkedWindows : public QObject
{
    Q_OBJECT

public:
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
        // Drawn here instead of `shown` while hovered (see HoverPreviews::update).
        std::optional<QRectF> preview = std::nullopt;
        // Animating from `from` to `shown` since `start`; `current`: where
        // it is drawn in this frame (see advance), so drawing and input
        // agree on one place per frame.
        bool animating = false;
        QRectF from = {};
        std::chrono::steady_clock::time_point start = {};
        QRectF current = {};
    };

    // --- Which windows ---

    // Parked or restoring.
    bool isParked(Window *window) const;
    bool isParkedNotRestoring(Window *window) const;
    bool empty() const;
    Parked *find(Window *window);
    const Parked *find(Window *window) const;
    Parked &at(Window *window);
    const Parked &at(Window *window) const;
    const std::map<Window *, Parked> &all() const;
    // Set or drop a window's entry as is, nothing else (callers draw it).
    void set(Window *window, const Parked &parked);
    void erase(Window *window);

    // --- Places ---

    // Where a window is: from where it is drawn when parked, else from its
    // frame (see glance::placeOfFrame).
    Place placeOf(Window *window) const;
    // Where `window` goes in `place` (see glance::placeRect), on its monitor.
    QRectF placeRect(Window *window, Place place, const QSizeF &size, qreal centerY, qreal stash = stashScale) const;
    // Move a window to `place`, gliding from where it is drawn. `scale`:
    // for a stash, the scale to show it at.
    void moveTo(Window *window, Place place, qreal scale = stashScale);
    // `window` to its stop in `half` of main at its full (unparked) size,
    // keeping its vertical center, gliding (Meta+Left/Right, see
    // nextMainStop).
    void moveToMainStop(Window *window, Side half);
    // `window` into `half` of main at the full usable height, gliding.
    void fillHalf(Window *window, Place half);
    // Put a window of full size `size` in `place` (see placeRect), gliding
    // from `from`.
    void commitPlace(Window *window, Place place, const QSizeF &size, qreal centerY, const QRectF &from,
                     qreal stash = stashScale);
    // Really resize (and move) a window to `target`, drawing it gliding
    // there from `from`: until the app has its new size and the animation
    // is over, it is drawn scaled (the "restoring" state).
    void resizeAnimated(Window *window, const RectF &target, const QRectF &from);
    // Park a window: draw it at `shown`, and really resize the app (see
    // layoutSize). `original` is its size before it was first parked.
    // At parking size it becomes a tile (see glance::parkingTileRect)
    // instead, centered where `shown` is: callers animate from `shown`.
    void park(Window *window, const QRectF &shown, const QSizeF &original);
    // Minimize = park: a window being minimized is shown again and goes to
    // the parking area on the side nearer to it (see the .cpp).
    void minimizeToParking(Window *window);
    // The scale at which `windows`, at their full sizes, fit in one column
    // (see glance::fittingScale).
    qreal fittingScale(LogicalOutput *output, const std::vector<Window *> &windows) const;
    // The size to really resize a parked window to (see
    // glance::layoutSize). Clips in parking are laid out at twice the
    // shown size, whatever parkingLayoutWidth: drawn at 1/2, the text is small
    // but readable and reflows into a narrow note, and the hover preview
    // (at most 1:1) doubles it. Clips in a stash keep their full width,
    // only drawn smaller, like any window (2026-10-05; from 2026-10-04
    // drawn 1:1 with full-size text, which didn't look smaller). The clip
    // app then picks the height its text needs (see Clips::frameChanged).
    static QSizeF layoutSize(Window *window, const QSizeF &shown, const QSizeF &original);

    // --- Drawing ---

    // Where a managed window's frame is drawn: `shown` (or its preview), or
    // while animating where this frame has it (see advance).
    static QRectF displayRect(const Parked &parked);
    // The scale a managed window's current frame is drawn at.
    qreal scaleOf(Window *window) const;
    // Where a parked window is drawn (flat, see tiltOf).
    QRectF drawnRect(Window *window) const;
    // Whether `pos` is on what is drawn of a parked window (tilt included).
    bool drawnContains(Window *window, const QPointF &pos) const;
    // Where a window is drawn now (parked or not).
    QRectF currentlyDrawn(Window *window) const;
    // Draw a parked window at its place, whatever its frame's current
    // position and size. A restoring window is done once it has its
    // original size; then it just needs to be where it is drawn.
    void applyParked(Window *window);
    // KWin's interactive resize (a window edge, Meta+right-drag) of a
    // parked window: while it lasts, the drawing follows the frame at the
    // window's scale, and the window stays parked where it is drawn.
    void resizeStarted(Window *window);
    void resizeFinished(Window *window);
    // Start animating a parked window from `from` to its `shown` place.
    void animate(Window *window, const QRectF &from);
    // Each frame, before painting: move animating windows on (ease-out);
    // finished ones settle. Changing a window's transform repaints just
    // what it covered and covers now.
    void advance();
    bool anyAnimating() const;
    // After painting: ask for the next frame where windows are animating
    // (no damage: advance moves them, which damages what changed).
    void scheduleFrames();
    // Set how any window is drawn (see applyParked; dragging uses it too).
    void setDrawTransform(Window *window, const QTransform &transform);

    // --- Tilt ---

    // Whether `window` is drawn turned (also while turning flat after it
    // left parking, e.g. dragged out).
    bool isTilted(Window *window) const;
    bool anyTilted() const;
    // How a window is turned, applied after its draw transform: in the
    // item's coordinates (relative to the frame's top-left corner, for
    // drawing), or global ones. Identity when flat.
    QTransform tiltOf(Window *window) const;
    QTransform globalTiltOf(Window *window) const;
    // Where a point drawn at global `pos` would be drawn flat.
    QPointF untilt(Window *window, const QPointF &pos) const;
    // The window really visible at `pos`, like InputRedirection::findToplevel
    // but using the drawn rectangle for parked windows (unless drawn
    // exactly over the frame: then KWin's own hit test, which includes
    // the decoration's resize borders outside the frame).
    Window *pick(const QPointF &pos, Window *ignore = nullptr) const;
    // A parked window that acts like an icon: anything in parking, and a
    // stashed one shown small enough (see iconBelow). Narrow windows (clips,
    // Firefox at its 500 px minimum) are drawn above iconBelow in parking
    // (see parkingScale) but are icons there all the same.
    bool isIcon(Window *window) const;
    // A window one can select (Meta+Alt+arrows, Alt+Tab): one that is shown
    // on the screen (not e.g. KDE's hidden Xwayland Video Bridge, which then
    // can't be activated and blocks the way).
    bool switchable(Window *window) const;

    // --- Making room ---

    // Stash or parking area, left or right, of a parked window: 0 parking
    // left, 1 stash left, 2 parking right, 3 stash right.
    int areaOf(Window *window) const;
    static bool isParkingArea(int area);
    // Re-form the column of windows in one area on `output`: stacked with
    // arrangeGap between them, centered vertically in the usable screen
    // area, ordered by vertical center (an `arriving` window that lands on
    // another goes below it). Moved windows animate.
    void arrangeArea(int area, LogicalOutput *output, Window *arriving);
    // `window` has just arrived in a stash or parking area. Only parking
    // columns re-form: a stash keeps windows where they were put (they may
    // overlap); only declutter lines one up.
    void arrange(Window *window);
    // Before a parked window leaves its area: returns a function that, once
    // it has left, re-forms the area it left (parking only, see arrange).
    std::function<void()> leaving(Window *window);

    // --- Lifetime ---

    // A window closed: forget it, and close the gap it leaves.
    void closed(Window *window);
    // Unloading: every parked window back to full size, where it is drawn.
    void restoreAll();
    // Logging out: apps remember their window size when they are closed at
    // logout (Firefox reopens at it), so every parked window really gets its
    // original size back first. They stay parked, drawn where they are, so
    // nothing changes on the screen.
    void fullSizeForLogout();

Q_SIGNALS:
    // A window's draw transform changed (its focus ring follows it).
    void transformChanged(KWin::Window *window);

private:
    static qreal progress(std::chrono::steady_clock::time_point start);
    // Turn towards what the window's state wants (in parking, not
    // previewed: turned; anything else: flat), animated.
    void updateTilt(Window *window);
    // Drawn exactly over its frame (scale 1, lined up).
    bool drawnAtFrame(Window *window) const;

    std::map<Window *, Parked> m_parked;
    // A parked window KWin is resizing: its drawing and frame at the start.
    struct Resizing
    {
        Window *window;
        QRectF drawn;
        RectF frame;
    };
    std::optional<Resizing> m_resizing;
    // Windows turned or turning (see updateTilt): `amount` now, 0 flat to
    // 1 tiltAngle, animating from `from` to `to` since `start`.
    struct Tilt
    {
        Side side = Side::Left;
        qreal amount = 0;
        qreal from = 0;
        qreal to = 0;
        bool turning = false;
        std::chrono::steady_clock::time_point start = {};
    };
    std::map<Window *, Tilt> m_tilts;
};

} // namespace glance
