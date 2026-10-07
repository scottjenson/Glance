// Glance's geometry: where windows go and how large they are drawn. Pure
// functions on rectangles and sizes (global logical coordinates), with no
// KWin in them, so the unit tests (kwin/tests) can check them directly.
// `screen` is a monitor's full rectangle; `area` the part windows may use
// (KWin's MaximizeArea: the screen minus panels).
#pragma once

#include "tuning.h"

#include <QRectF>
#include <QSizeF>
#include <QTransform>

#include <optional>
#include <vector>

namespace glance
{

// --- Shrinking at the edges ---

// Scale at which a window, scaled around the cursor, has its edge on one
// side at the point of the curve: full size until that edge enters the
// edge zone, then falling linearly to minScale at the screen edge.
// `cursorToScreenEdge` and `cursorToWindowEdge` are measured towards the
// same side, the latter at full size. The drawn edge is at distance
// d = cursorToScreenEdge - cursorToWindowEdge * s from the screen edge,
// and s = minScale + (1 - minScale) * d / zoneWidth; solved for s.
qreal edgeScale(qreal cursorToScreenEdge, qreal cursorToWindowEdge, qreal zoneWidth);

// The edge rule's scale `rule` remapped for a window dragged out of a
// stash at scale `hold`, when the rule gave `from` at the drag's start:
// `from` gives `hold`, 1 gives 1 and `floor` (parking size) gives `floor`,
// linear in between (see WindowDrag::holdScale).
qreal heldScale(qreal rule, qreal from, qreal hold, qreal floor);

// How far a box spanning [x, x + width) must move horizontally to lie
// inside `screen`. A box wider than the screen keeps its left edge visible.
qreal shiftOntoScreen(qreal x, qreal width, const QRectF &screen);

// The scale of a window of full size `original` in parking: minScale,
// or more if its longer side would be shorter than parkingMinSize.
qreal parkingScale(const QSizeF &original);

// Whether a window drawn `shownWidth` wide is at parking size (see
// parkingScale), which makes it a parking icon (see parkedPlace).
bool atParkingSize(qreal shownWidth, const QSizeF &original);

// The app's real size for a window parked at `shown` (drawn size) whose
// full size is `original` and whose app needs at least `appMin` (frame
// size): in a stash its full size (only drawn smaller); in parking (drawn
// at parking size, see parkedPlace) a square parkingLayoutWidth wide (no
// larger than the original's longer side), each side at least `appMin`'s.
QSizeF layoutSize(const QSizeF &shown, const QSizeF &original, const QSizeF &appMin);

// --- Parking tiles ---

// A parking icon's tile: a window of shape `shape` (its layout size) drawn
// parkingTile on its longer side (less for a window smaller than that at
// parking size, see parkingScale; `original` is its full size), against
// the screen edge on `side`, centered at `centerY` (kept in `area`).
QRectF parkingTileRect(Side side, const QSizeF &shape, const QSizeF &original, qreal centerY, const QRectF &screen,
                       const QRectF &area);

// A parking icon drawn at `rect`, turned away from the viewer by
// `amount` (0: flat, 1: tiltAngle) around the vertical line through its
// edge on `outer` (the screen edge it is against), seen in perspective
// from an eye level with `eyeY`, `distance` away: that edge stays where
// it is, the rest recedes towards the eye's level. Every icon of a
// column turned with the same eye lies on one plane (their edges all run
// to one vanishing point). Maps where a point is drawn flat to where it
// is drawn turned.
QTransform tiltTransform(const QRectF &rect, Side outer, qreal amount, qreal eyeY, qreal distance);

// --- Places ---

// The places Meta+Left/Right and gestures step along.
inline constexpr Place placeOrder[] = {Place::ParkingLeft, Place::StashLeft, Place::HalfLeft,
                                       Place::HalfRight, Place::StashRight, Place::ParkingRight};
int placeIndex(Place place);

// For a window not parked, with frame `frame`: all of main or a half of it
// (see halfMatch), or free.
Place placeOfFrame(const QRectF &frame, const QRectF &screen);

// Meta+Left/Right for a window not parked, with frame `frame`: which
// half of main to put it in next, keeping its size (centered in that
// half, kept inside main), always moving towards `direction`; none when
// it is already at (or past) the last stop: then it goes on into the
// stash. A window as wide as main has one stop, so it goes straight on.
std::optional<Side> nextMainStop(Side direction, const QRectF &frame, const QRectF &screen);

// A window of full size `size` at its stop in `half` of main (see
// nextMainStop), keeping its vertical center where there is room.
QRectF mainStopRect(Side half, const QSizeF &size, qreal centerY, const QRectF &screen, const QRectF &area);

// For a parked window drawn at `shown` with full size `original`: the
// stash or parking area on the side its center is on (parking when drawn
// at parking size).
Place parkedPlace(const QRectF &shown, const QSizeF &original, const QRectF &screen);

// Where a window of full size `size`, centered at `centerY`, goes in
// `place`: for the halves and all of main its new frame, for a stash or
// parking area where it is drawn (a stash at `stash` scale; parking as a
// tile of the window's own shape, see parkingTileRect). Empty for
// Place::Free.
QRectF placeRect(Place place, const QSizeF &size, qreal centerY, const QRectF &screen, const QRectF &area,
                 qreal stash = stashScale);

// The scale (at most stashScale, a little above minScale so it stays a
// stash) at which windows of full heights `heights` fit in one column in
// `areaHeight`, with arrangeGap between them.
qreal fittingScale(const std::vector<qreal> &heights, qreal areaHeight);

// --- Rectangles ---

QRectF lerpRect(const QRectF &a, const QRectF &b, qreal e);

// Scale a box around `pos` from size `from` to `to`.
QPointF scaledTopLeft(const QPointF &topLeft, const QPointF &pos, const QSizeF &from, const QSizeF &to);

// Moved as little as needed to lie within `area` (a bigger box keeps
// its top left corner in).
QRectF keptIn(const QRectF &box, const QRectF &area);

// --- The Alt+Tab map ---

// Spread piled-up windows apart, for the map. `windows` are where the map
// draws them, front first. Windows overlapping by more than pileOverlap
// of the smaller one form a pile. The front window of a pile stays; the
// others go, alternately, into a row above and a row below the pile,
// within its width and the space up to the edge of `screen`, shrunk until
// the row fits. Returns, for each window, where it goes (none: it stays).
std::vector<std::optional<QRectF>> spreadPiles(const std::vector<QRectF> &windows, const QRectF &screen);

} // namespace glance
