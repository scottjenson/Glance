# Code review: architecture and performance

Reviewed 2026-10-03 at commit 85f79ea (line numbers below refer to that
commit). Asked for by the user: no new features, only whether the approach
is sound, reliable and efficient, especially for drawing and animation.
None of the fixes are built yet; they need the user's go-ahead (finding 1
needs a design talk first). Also kept as a shared doc:
https://claude.ai/code/artifact/389dfb06-2d1f-40b3-ba47-22d86aa2c61d

## Summary

The approach is sound: Glance draws windows through KWin's scene graph and keeps each app's real frame in step, which is the right way to do this in KWin 6. The risks are in a few places where per-frame or per-motion work is heavier than it needs to be, and in one hack (moving invisible frames under the pointer) that has side effects beyond input.

Reviewed: `kwin/main.cpp` (the effect, 3,800 lines, read in full), checked against the KWin 6.7.5 source in `~/src/kwin-6.7.5`. `glance-clip` was only looked at where the effect talks to it. Every item below is about reliability or performance.

Open question for the user (asked 2026-10-03, not answered yet): will
testers have a second monitor next to the ultrawide? If yes, do the full
fix for finding 1; if not, the minimal guard is enough for now.

## Critical improvements

Five changes, in order of importance. The first three touch every frame or every pointer motion; the last two are about failure modes and keeping the code safe to change.

| # | Issue | What it costs | Effort |
| --- | --- | --- | --- |
| 1 | Parked windows' real frames move on every pointer motion | Per-motion KWin work; wrong monitor on multi-monitor setups | Medium (design talk) |
| 2 | Effect blocks direct scanout whenever anything is parked | Fullscreen video and games always composited | Small |
| 3 | Animations repaint the whole screen every frame | GPU fill of ~14.7 M pixels per frame on this display | Small to medium |
| 4 | Clip data is decoded and saved on the compositor thread, unbounded | Desktop freezes on big images; possible session crash | Small |
| 5 | One 3,800-line class, ~15 mode flags, no tests | Cross-feature bugs; every change gets riskier | Ongoing |

### 1. Re-anchoring moves real frames on every pointer motion

**Status (2026-10-04):** step 1 built and checked by the user:
re-anchoring at most once per refresh for motion and scrolling
(forwarding in between, a late re-anchor where the pointer stops), at once
for buttons, and never across to another monitor. Agreed with the user:
stashed windows must stay fully interactive, so hover behaviour must not
change. The hybrid (forward motion; move the frame only on enter, pause
~100 ms, press, and over the title bar) is paused: only if step 1 turns
out not to be enough (user, 2026-10-04); see
[docs/dragging-and-parking.md](../docs/dragging-and-parking.md).

**Where:** `route()` calls `reanchor()` (main.cpp:3660) for every motion over a parked window (onMotion, main.cpp:609; route, main.cpp:3774), then `pointer->update()` re-picks.

**Why it matters:** each `window->move()` is a real geometry change in KWin. In 6.7.5 that runs `WaylandWindow::updateGeometry`, which re-evaluates window rules, re-computes the window's output from the frame's center, and sends `wl_surface.enter/leave` to the app; X11 apps also get a ConfigureNotify. That is a lot of work per mouse event (hundreds per second on a gaming mouse).

It is also a correctness bug with a second monitor. The frame is full layout size (at least 400 px) while the drawing is ~180 px, so the frame swings far past the drawing. Example: left parking at scale 0.45, pointer near the icon's right side: the frame's left edge lands near x = -217 and its center at about x = -17, off the ultrawide. KWin then assigns the window to the monitor on the left. Glance reads `window->output()` everywhere (`areaOf`, `arrangeArea` at main.cpp:2036, `placeOf`), so the icon can drop out of its column, and with a different scale on that monitor the app re-renders at another scale.

**Fix (needs a design talk):** move the frame only when it matters to KWin, i.e. when the pointer enters a parked window and on a button press. Between those, deliver motion through the existing forwarding path (`setFocusedPointerSurfaceTransformation` with `transformFor`), which changes no geometry. As a minimum guard, fall back to forwarding whenever the re-anchored frame's center would fall outside the window's own output.

### 2. Direct scanout is blocked while anything is parked

**Status (2026-10-04):** built (`Glance::blocksDirectScanout`): blocks
only during a drag, a bounce, the Alt+Tab map, a clip drag or a parked
window's animation. KWin's scanout check maps items through their
transforms, so parked windows at rest are accounted for; it also covers
overlay planes (e.g. a video in a window), not just full screen. Can't
be seen in the VM (no scanout on VMware's GPU); checked by the user:
nothing looks different. Low priority for the user: full screen makes
little sense on an ultrawide, but it's cheap and right.

**Where:** `isActive()` (main.cpp:349) is true whenever `m_parked` is not empty, which in Glance is nearly always. `Effect::blocksDirectScanout()` defaults to `true` (kwin effect/effect.cpp:535), and KWin skips direct scanout if any active effect blocks it.

**Why it matters:** a fullscreen video or game can normally be sent straight to the display, skipping composition. With Glance loaded and one icon parked, every frame is composited: more GPU work, more power, a little more latency. This is invisible in the VM but real on the target desktops.

**Fix:** override `blocksDirectScanout()` to return true only while something is actually moving or overlaid: a drag, the Alt+Tab map, a clip drag, a bounce, or a parked window animating. Static parked windows don't need it: a fullscreen window covers them.

### 3. Animations repaint the whole screen every frame

**Status (2026-10-04):** built and checked by the user. Parked
animations and drag glides no longer repaint the screen: the transform
change damages what moved, and `Item::scheduleFrame` (no damage) keeps
them ticking (`ParkedWindows::scheduleFrames`,
`WindowDrag::postPaintScreen`). The map asks for frames only while it
opens or closes, and a Tab only repaints with the map up (the ring item
repaints itself). The bounce repaints its window's bounds. One clock per
frame: `advance()` stores each animating window's rectangle
(`Parked::current`) and `displayRect` returns it, so drawing, `pick` and
re-anchoring agree. Left as is: a clip drag and the map still redraw the
whole screen on each of their frames, since KWin's "transformed windows"
painting mode (needed for them) ignores damage. Headless: parking a big
window leaves no ghost; the map opens fully.

**Where:** `postPaintScreen()` (main.cpp:480) calls `effects->addRepaintFull()` every frame while any parked window animates, while a drag glides, and for the whole time the Alt+Tab map is up, even after it has finished opening and nothing moves. `animate()`, the bounce timers, `step()` and clip-drag motion do the same.

**Why it matters:** this display is about 6000×2450 device pixels. A full repaint re-composites every window on it, every frame, so a 180 ms parking glide costs as much as redrawing the whole desktop ~11 times. `Item::setTransform()` already schedules a repaint of the item's old and new bounds (kwin scene/item.cpp:253), so `applyParked()` alone damages exactly what moved.

**Fix:**

- Parked windows: drop `addRepaintFull()`; the transform change already asks for the next frame. Check with KDE's "Show Paint" desktop effect that only the moving window flashes.
- Map: repaint while it opens or closes; once fully open, only when the selection changes.
- Bounce and clip drag: repaint the affected window (`EffectWindow::addRepaintFull()`), not the screen.
- Animation clock: `displayRect()` calls `steady_clock::now()` each time it is asked, from painting, `pick()`, `scaleOf()` and `drawnRect()`, so one frame can see several different positions and input can hit-test a slightly different place than was drawn. Compute each animating window's rectangle once in `prePaintScreen()`, store it, and read the stored value everywhere else.

### 4. Clip data is handled on the compositor thread, without a size limit

**Status (2026-10-04):** built as proposed (cap `clipMaxBytes` 50 MB,
`QImageReader` header check, `QtConcurrent::run` for the write/copy),
checked by the user. Line numbers below are from before the
refactor ([refactor.md](refactor.md)).

**Where:** `readClip()` (main.cpp:2709) appends whatever the app sends with no cap. `finishClip()` (main.cpp:2727) then decodes the whole image just to check it (`QImage::fromData`, main.cpp:2755), writes the file, or copies a dropped image file of any size (`QFile::copy`, main.cpp:2781), all on KWin's main thread.

**Why it matters:** that thread paints every frame and handles every input event. Decoding a large screenshot PNG takes 100 ms or more, during which the whole desktop freezes. An app that streams a very large payload grows KWin's memory until the system kills it, which ends the session. The reading itself is already non-blocking (pipe plus `QSocketNotifier`), which is right.

**Fix:** cap the read (for example 50 MB, then give up with a log line). Check images with `QImageReader` on a `QBuffer`, which reads only the header. Move the file write and copy off the main thread: hand the data to glance-clip (it can save its own file), or use `QtConcurrent::run` and start glance-clip when it finishes.

### 5. One large class with many modes and no tests

**Status (2026-10-04):** in progress, see [refactor.md](refactor.md)
(stages 1-4 done and checked by the user). Unit tests
for the geometry are in kwin/tests. Per-window state is still in a few
components (each erases its own on close, called from Glance::watch).

**Where:** all of the effect is one class in one file, with about 15 independent mode states (`m_dragged`, `m_pending`, `m_clip`, `m_clipDrag`, `m_switch`, `m_map`, `m_preview`, `m_previewCandidate`, `m_noPreview`, `m_wheelWindow`, `m_declutter`, `m_bounce`, `m_chosen`, ...). Each input handler checks them in its own hand-written order (`onMotion`, main.cpp:593; `onButton`, main.cpp:619). Per-window state is spread over `m_parked`, `m_wasFull`, `m_recent` and the map, all keyed by raw `Window*` and cleaned up in one `closed` handler (main.cpp:1015).

**Why it matters:** the bugs a window manager ships are usually at the seams: Alt+Tab pressed during a Meta+wheel settle, a preview open when a declutter runs, a window closing mid-animation. With the logic spread like this, each new feature makes those harder to see, and nothing catches a regression except the user's hands.

**Fix:**

- Split the file by area: geometry and places (pure functions), the parked-window model and animation, input routing, the Alt+Tab map, clips.
- Add unit tests for the pure geometry: `edgeScale`, `layoutSize`, `placeRect`, `placeOfFrame`, `fittingScale`, `spreadPiles`. They need no running KWin and run in milliseconds.
- Keep all per-window state in one struct, erased in one place when a window closes.

## Done well

The core rendering choices are the efficient ones, and most of the hard KWin edge cases are handled deliberately and documented. Keep these.

- **Drawing through the scene graph.** Shrinking is a `QTransform` on the window's `WindowItem` (`setDrawTransform`, main.cpp:3514), not an offscreen copy or a custom render pass. The GPU draws each window once, at its drawn size, and KWin keeps stacking, damage and occlusion. This is the cheapest correct way to scale live windows in KWin 6.
- **Apps really resize when parked.** `layoutSize()` (main.cpp:1912) gives a parked app a small real size, preferring exactly 1:1 or 2:1 of the drawn size. Apps then render far fewer pixels, textures stay small, and text stays sharp instead of being a blurry downscale of a full-size window.
- **The focus ring is a child item.** `OutlinedBorderItem` hangs off the window's item (main.cpp:3565), so it moves, scales and stacks with the window for free, with no drawing code of its own.
- **Minimal effect hooks.** `isActive()` keeps the effect out of painting when nothing is shrunk, and `setTransformed()` is set only for windows that need it. The clipping workarounds (finite regions in `paintWindow`, main.cpp:398) come with the root cause in KWin's source written next to them.
- **Async where it counts.** Clipboard and drag data are read through a non-blocking pipe with a `QSocketNotifier` and timeouts (main.cpp:2684), never a blocking read.
- **Safe lifetimes across async steps.** Windows held across timers and deferred calls are `QPointer`s and checked before use; the bounce uses a generation counter so stale timers do nothing; the label texture is deleted only with the GL context current.
- **Reversible changes to KDE.** Quick tiling, the Meta+drag command and the disabled shortcuts are changed in memory only, re-applied if settings reload, and restored on unload; parked windows go back to full size. Nothing is written to the user's KDE config.
- **Light input interception.** The filter passes events on by default and takes them only for its own gestures, so KWin's normal handling stays in charge.
- **Readable intent.** Tuning values are named constants in one place, and comments explain why, which made this review much faster.

## Smaller notes

Worth knowing, not urgent.

- **Reliance on KWin internals.** Glance uses private headers (`kwin/kwin-private`), calls `cancelModiferOnlySequence` by name, and casts `workspace()->tabbox()` to `QObject*` (main.cpp:1121). Any of these can silently stop working, or crash, after a Plasma update. Before others try it, log a clear warning (or refuse to load) when the running KWin version differs from the one Glance was built against.
- **Parked state lives only in memory.** If KWin crashes or restarts, parked apps keep their small layout sizes and come back as small, oddly placed full-size windows. A known limit rather than a bug; worth one line in the README.
- **Order of `std::map<Window*, ...>` is by memory address.** Where two entries could match (for example `iconAt()` with overlapping spots, main.cpp:2285), which one wins is effectively random. Harmless today; sort by position if it ever matters.
- **The ring is rebuilt every frame while the map is up.** `prePaintScreen()` calls `updateRing()` each frame (main.cpp:475), which re-reads the palette and resets the outline. Only needed while the map is opening or closing.
- **Multi-monitor is largely untested.** Beyond finding 1, the map opens on `activeOutput()` and switching checks only the window's own output. Fine for one ultrawide; worth a pass before people with two monitors try it.
