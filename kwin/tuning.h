// Glance's tuning knobs, and the places windows go: shared by every part
// of the effect (and the unit tests), so they live in one place.
#pragma once

#include <QtGlobal>

#include <chrono>

namespace glance
{

enum class Side { Left, Right };
// Where a window is, for Meta+Left/Right. Each side has a parking area, a
// stash and a half of main; Full is all of main (see Meta+Down); Free is
// anywhere else.
enum class Place { ParkingLeft, StashLeft, HalfLeft, HalfRight, StashRight, ParkingRight, Full, Free };

// --- Tuning knobs ---
// Width of the left and right edge zones, where shrinking happens, as a
// fraction of the screen width: main (the middle half) stays full size.
inline constexpr qreal zoneFraction = 0.25;
// Window size at the very edge of the screen (1.0 = full size).
inline constexpr qreal minScale = 0.15;
// But no smaller than this on its longer side (logical pixels): small
// windows (clips, dialogs) would be specks at minScale (see parkingScale).
// The longer side, not the width (2026-10-04): a tall narrow image clip
// was a 180x384 "icon", hardly smaller than the window, and anywhere in
// the edge zone counted as parking.
inline constexpr qreal parkingMinSize = 180.0;
// Stashed windows drawn smaller than this (relative to the original size)
// act like icons: drag anywhere to move, click passes through. Larger
// ones stay normal windows, so their content keeps every interaction.
// Windows in parking are always icons, whatever their scale.
inline constexpr qreal iconBelow = 0.25;
// How far (logical pixels) a press on an icon must move to become a drag.
inline constexpr qreal dragThreshold = 6.0;
// Dropped at this scale or larger, a window goes back to full size.
inline constexpr qreal parkBelow = 0.99;
// Zoom in the stash, reflow in parking (2026-10-05, user: shrinking
// should be seen, and it gives depth; pages reflowing at every drop was
// disruptive): a stashed window keeps its app's full size, only drawn
// smaller; a parking icon's app is laid out this wide (keeping its shape,
// at least the app's minimum), so web pages switch to their phone
// layout and the icon shows the app's compact form.
inline constexpr qreal parkingLayoutWidth = 600.0;

// A window counts as being in a half of main when its horizontal
// extent and the half's share at least this much (intersection over
// union), so a slightly moved or resized one still does.
inline constexpr qreal halfMatch = 0.8;

// Hover previews: how much a hovered icon grows, the wait before the
// first one opens, and the grace before one closes after the pointer
// left.
inline constexpr qreal previewGrow = 2.0;
inline constexpr std::chrono::milliseconds previewDelay{300};
inline constexpr std::chrono::milliseconds previewGrace{300};

// Meta+wheel (see MetaWheel::axis): size change per unit of scroll (a mouse
// wheel notch is 15 units; touchpads send smaller, more frequent steps),
// the smallest size a window in main gets (logical pixels), and how
// long after the last scroll a stashed window's app gets its new size.
inline constexpr qreal wheelStepWheel = 0.1 / 15.0;
inline constexpr qreal wheelStepFinger = 0.1 / 40.0;
inline constexpr qreal wheelMinWidth = 300.0;
inline constexpr qreal wheelMinHeight = 200.0;
inline constexpr std::chrono::milliseconds wheelSettle{400};
// A Meta press counts as a tap (opens KDE's launcher) between these (see
// metaKey).
inline constexpr std::chrono::microseconds metaTapMin{10'000};
inline constexpr std::chrono::microseconds metaTapMax{400'000};

// Where clips are saved, relative to the home folder, and the clip
// app's app id (kwin/clip/main.cpp), which tells clip windows apart.
inline constexpr const char *clipsFolder = "Clips";
inline constexpr const char *clipAppId = "org.glance.Clip";
// How long a pasted (moved) clip may take to close before it is shown
// back in its place.
inline constexpr std::chrono::milliseconds clipCloseWait{1500};
// The parking band: this close to a screen edge (fraction of the edge
// zone's width), dropped text becomes a parking icon (see placeClip),
// and a snapping drag snaps to parking (see snapTargetAt).
inline constexpr qreal parkingBand = 0.15;
// How long to wait for a dragging app to hand over its text, and its
// image (it may encode it first).
inline constexpr std::chrono::milliseconds clipTimeout{2000};
inline constexpr std::chrono::milliseconds clipImageTimeout{6000};
// The most data a clip takes (text or an image, in bytes): more is given up
// on, so an app can't grow KWin's memory without end.
inline constexpr qsizetype clipMaxBytes = 50 * 1024 * 1024;

// Width of the focus ring on screen (logical pixels).
inline constexpr qreal ringWidth = 4.0;
// Bounce of a window getting the focus ring: its scale frame by
// frame (the first is shown at once), and the time between frames.
inline constexpr qreal bounceFrames[] = {1.0, 0.99, 0.98, 0.99, 1.0};
inline constexpr std::chrono::milliseconds bounceStep{60};

// Scale of a window in a stash when put there with the keyboard, and the
// most of the zone's width it may take there.
inline constexpr qreal stashScale = 0.5;
inline constexpr qreal stashMaxWidth = 0.6;
// But at least this on its longer side (logical pixels; or nearly its
// full size, if smaller), so a small window's stash is clearly bigger
// than its parking icon (1.5 times parkingMinSize; 2026-10-04, the
// user's call).
inline constexpr qreal stashMinSize = 1.5 * parkingMinSize;
// Length of keyboard moves and making-room animations.
inline constexpr std::chrono::milliseconds animationTime{180};
// Vertical gap between windows that made room for each other.
inline constexpr qreal arrangeGap = 8.0;
// Meta+drag acceleration (see leadStep): the highest gain, reached after
// moving leadBuild (fraction of the screen width) in one direction; a
// reversal is this much movement the other way (less is jitter); below
// leadSlow (logical px/s over the last leadSampleTime) it's 1:1 again.
// Pause to snap: holding still this long snaps; the middle band of the
// screen (fraction of its width) where it snaps to all of main.
inline constexpr qreal leadMaxGain = 4.0;
inline constexpr qreal leadBuild = 0.05;
inline constexpr qreal reversalJitter = 5.0;
inline constexpr qreal leadSlow = 300.0;
inline constexpr std::chrono::milliseconds leadSampleTime{80};
inline constexpr std::chrono::milliseconds snapDwell{500};
inline constexpr qreal snapFullBand = 0.2;
// Alt+Tab (see switchKey): Alt held this long after Tab shows the map;
// the map's scale (0.5: the desktop fits in main's width); how long it
// takes to open (shrink and spread at once) and to close;
// the brightness of windows other than the selection. Windows
// overlapping more than pileOverlap (of the smaller one's area) form a
// pile; the gap between spread windows, and their smallest scale.
inline constexpr std::chrono::milliseconds holdDelay{200};
inline constexpr qreal mapScale = 0.5;
inline constexpr std::chrono::milliseconds mapTime{200};
inline constexpr qreal mapDim = 0.45;
inline constexpr qreal pileOverlap = 0.1;
inline constexpr qreal spreadGap = 12.0;
inline constexpr qreal spreadMinScale = 0.05;
// The label (logical pixels): icon size, title text size, the widest
// the title gets (longer ones are cut with "..."), padding, gap between
// icon and title, corner radius.
inline constexpr qreal labelIconSize = 40.0;
inline constexpr int labelTextSize = 22;
inline constexpr qreal labelMaxWidth = 600.0;
inline constexpr qreal labelPadding = 10.0;
inline constexpr qreal labelGap = 10.0;
inline constexpr qreal labelRadius = 12.0;

} // namespace glance
