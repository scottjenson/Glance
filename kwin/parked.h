// Parked windows: the windows Glance draws somewhere other than their real
// frame (in a stash, in parking, or gliding back to full size), their
// places, animation and making room, and how every window is drawn.
//
// A parked window has a real frame (what the app and KWin's input know,
// resized to a small layout size, see layoutSize) and a drawn rectangle
// (`shown`); a transform on its scene item fits the one into the other
// (applyParked). Keeping the two in step is most of Glance.
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
        // Animating from `from` to `shown` since `start`.
        bool animating = false;
        QRectF from = {};
        std::chrono::steady_clock::time_point start = {};
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
    void park(Window *window, const QRectF &shown, const QSizeF &original);
    // The scale at which `windows`, at their full sizes, fit in one column
    // (see glance::fittingScale).
    qreal fittingScale(LogicalOutput *output, const std::vector<Window *> &windows) const;
    // The size to really resize a parked window to (see
    // glance::layoutSize). Clips in parking are laid out at twice the
    // shown size, whatever minLayoutWidth: drawn at 1/2, like a clip in a
    // stash, the text is small but readable and reflows into a narrow
    // note, and the hover preview (at most 1:1) doubles it. The clip app
    // then picks the height its text needs (see Clips::frameChanged).
    static QSizeF layoutSize(Window *window, const QSizeF &shown, const QSizeF &original);

    // --- Drawing ---

    // Where a managed window's frame is to be drawn right now: `shown` (or
    // its preview), or on the way there (ease-out).
    static QRectF displayRect(const Parked &parked);
    // The scale a managed window's current frame is drawn at.
    qreal scaleOf(Window *window) const;
    // Where a parked window is drawn.
    QRectF drawnRect(Window *window) const;
    // Where a window is drawn now (parked or not).
    QRectF currentlyDrawn(Window *window) const;
    // Draw a parked window at its place, whatever its frame's current
    // position and size. A restoring window is done once it has its
    // original size; then it just needs to be where it is drawn.
    void applyParked(Window *window);
    // Start animating a parked window from `from` to its `shown` place.
    void animate(Window *window, const QRectF &from);
    // Each frame: redraw animating windows at their current place;
    // finished ones settle.
    void advance();
    bool anyAnimating() const;
    // Set how any window is drawn (see applyParked; dragging uses it too).
    void setDrawTransform(Window *window, const QTransform &transform);
    // The window really visible at `pos`, like InputRedirection::findToplevel
    // but using the drawn rectangle for parked windows.
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

Q_SIGNALS:
    // A window's draw transform changed (its focus ring follows it).
    void transformChanged(KWin::Window *window);

private:
    static qreal progress(const Parked &parked);

    std::map<Window *, Parked> m_parked;
};

} // namespace glance
