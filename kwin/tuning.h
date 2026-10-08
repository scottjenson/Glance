// Glance's tuning knobs and the places windows go, shared by the effect
// and the unit tests. Sizes are logical pixels. The docs/ files explain
// what each feature does with them.
#pragma once

#include <QtGlobal>

#include <chrono>

namespace glance
{

enum class Side { Left, Right };
// Where a window is, for Meta+Left/Right. Full is all of main (Meta+Down);
// Free is anywhere else.
enum class Place { ParkingLeft, StashLeft, HalfLeft, HalfRight, StashRight, ParkingRight, Full, Free };

// --- Shrinking and parking (docs/dragging-and-parking.md) ---
// Width of each edge zone, as a fraction of the screen width.
inline constexpr qreal zoneFraction = 0.25;
// Scale at the very edge of the screen.
inline constexpr qreal minScale = 0.15;
// But parking icons are at least this on their longer side.
inline constexpr qreal parkingMinSize = 180.0;
// Stashed windows drawn smaller than this act like icons (windows in
// parking always do).
inline constexpr qreal iconBelow = 0.25;
// How far a press on an icon must move to become a drag.
inline constexpr qreal dragThreshold = 6.0;
// Dropped at this scale or larger, a window goes back to full size.
inline constexpr qreal parkBelow = 0.99;
// A parking app is laid out as a square this large (its phone layout).
inline constexpr qreal parkingLayoutWidth = 600.0;
// Parking tiles: drawn this large on their longer side, turned this many
// degrees, seen from this far (in usable screen heights; larger is flatter).
inline constexpr qreal parkingTile = 140.0;
inline constexpr qreal tiltAngle = 40.0;
inline constexpr qreal tiltDistance = 1.0;
// Windows drawn smaller than this are drawn from mipmaps.
inline constexpr qreal mipmapBelow = 0.5;

// --- Keyboard (docs/keyboard.md) ---
// A window is in a half of main when its horizontal extent and the half's
// share at least this much (intersection over union).
inline constexpr qreal halfMatch = 0.8;
// A Meta press counts as a tap (opens KDE's launcher) between these.
inline constexpr std::chrono::microseconds metaTapMin{10'000};
inline constexpr std::chrono::microseconds metaTapMax{400'000};
// Scale of a window put in a stash by keyboard, the most of the zone's
// width it may take, and the least on its longer side (or nearly full
// size, if smaller).
inline constexpr qreal stashScale = 0.5;
inline constexpr qreal stashMaxWidth = 0.6;
inline constexpr qreal stashMinSize = 1.5 * parkingMinSize;
// Keyboard moves and making-room animations.
inline constexpr std::chrono::milliseconds animationTime{180};
// Vertical gap between windows in a column.
inline constexpr qreal arrangeGap = 8.0;

// --- Hover previews (docs/hover-previews.md) ---
inline constexpr qreal previewGrow = 2.5;
inline constexpr std::chrono::milliseconds previewDelay{300};
inline constexpr std::chrono::milliseconds previewGrace{300};

// --- Meta+wheel (docs/meta-wheel.md) ---
// Size change per unit of scroll (a wheel notch is 15 units; touchpads
// send smaller steps), the smallest window in main, and how long after
// the last scroll a stashed window's app gets its new size.
inline constexpr qreal wheelStepWheel = 0.1 / 15.0;
inline constexpr qreal wheelStepFinger = 0.1 / 40.0;
inline constexpr qreal wheelMinWidth = 300.0;
inline constexpr qreal wheelMinHeight = 200.0;
inline constexpr std::chrono::milliseconds wheelSettle{400};

// --- Clips (docs/clips.md) ---
// Folder under home, and the clip app's app id.
inline constexpr const char *clipsFolder = "Clips";
inline constexpr const char *clipAppId = "org.glance.Clip";
// How long a pasted (moved) clip may take to close before it is shown
// back in its place.
inline constexpr std::chrono::milliseconds clipCloseWait{1500};
// The parking band, as a fraction of the edge zone's width: dropped clips
// and snapping Meta+drags go to parking there.
inline constexpr qreal parkingBand = 0.15;
// How long to wait for a dragging app's text, and its image.
inline constexpr std::chrono::milliseconds clipTimeout{2000};
inline constexpr std::chrono::milliseconds clipImageTimeout{6000};
// The most data a clip takes, in bytes.
inline constexpr qsizetype clipMaxBytes = 50 * 1024 * 1024;

// --- Focus ring (docs/focus-ring.md) ---
inline constexpr qreal ringWidth = 4.0;
// The bounce: scale per frame (the first shown at once), time between.
inline constexpr qreal bounceFrames[] = {1.0, 0.99, 0.98, 0.99, 1.0};
inline constexpr std::chrono::milliseconds bounceStep{60};

// --- Declutter (docs/declutter.md) ---
// Shaking a dragged window: this many changes of horizontal direction
// within shakeTime, each stroke at least shakeStroke px.
inline constexpr int shakeTurns = 3;
inline constexpr qreal shakeStroke = 30.0;
inline constexpr std::chrono::milliseconds shakeTime{500};

// --- Meta+drag (docs/meta-drag.md) ---
// Acceleration: the highest gain, reached after moving leadBuild (fraction
// of the screen width) in one direction; a reversal is this much movement
// the other way (less is jitter); below leadSlow (px/s over the last
// leadSampleTime) it's 1:1 again. Snapping: holding still this long, and
// the middle band (fraction of the screen width) that snaps to all of main.
inline constexpr qreal leadMaxGain = 4.0;
inline constexpr qreal leadBuild = 0.05;
inline constexpr qreal reversalJitter = 5.0;
inline constexpr qreal leadSlow = 300.0;
inline constexpr std::chrono::milliseconds leadSampleTime{80};
inline constexpr std::chrono::milliseconds snapDwell{500};
inline constexpr qreal snapFullBand = 0.2;

// --- Alt+Tab (docs/alt-tab.md) ---
// Alt held this long after Tab shows the map; the map's scale, opening
// and closing time, and the brightness of unselected windows. Piles:
// overlap (of the smaller one's area), gap between spread windows, their
// smallest scale.
inline constexpr std::chrono::milliseconds holdDelay{200};
inline constexpr qreal mapScale = 0.5;
inline constexpr std::chrono::milliseconds mapTime{200};
inline constexpr qreal mapDim = 0.45;
inline constexpr qreal pileOverlap = 0.1;
inline constexpr qreal spreadGap = 12.0;
inline constexpr qreal spreadMinScale = 0.05;
// The label: icon size, title text size, widest title (longer ones are
// cut with "..."), padding, gap between icon and title, corner radius.
inline constexpr qreal labelIconSize = 40.0;
inline constexpr int labelTextSize = 22;
inline constexpr qreal labelMaxWidth = 600.0;
inline constexpr qreal labelPadding = 10.0;
inline constexpr qreal labelGap = 10.0;
inline constexpr qreal labelRadius = 12.0;

} // namespace glance
