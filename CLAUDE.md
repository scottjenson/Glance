# WideMonitorUX: edge-shrink Wayfire plugin

## Goal
A window-management experiment for wide monitors: as the user drags a window
toward the left or right edge of the screen, the window shrinks, and windows
parked at the edge should feel like icons. The interaction was prototyped first
in HTML/JavaScript on a Mac; this project makes it real as a plugin for the
Wayfire Wayland compositor.

Wayfire was chosen over KWin effects because its view transformers handle both
rendering and input mapping (clicks land correctly on a scaled window).

**Ultimate goal: ship something people can try on KDE Plasma (KWin).** The
Wayfire plugin is the prototype; the design and algorithms should carry over,
the code mostly won't. Checked in KWin 6.7 source: a window's input transform
is hard-coded to a translation (`Window::inputTransformation()`, window.cpp),
so a scaled window being clickable needs an internal-API C++ plugin (e.g. an
input filter setting the seat's pointer surface transformation); KWin
scripts can't scale windows at all.

## KDE plan (decided 2026-09-28)
Options weighed:
- KWin script (JS): can't scale or map input; real resize alone can't reach
  the parked state (apps have minimum sizes). Ruled out.
- KWin effect: scales drawing but not input. Not enough on its own.
- **KWin C++ plugin (internal API): chosen.** Scale the window's scene item
  for drawing; an input filter picks the window by its drawn rectangle and
  sets the seat's pointer transformation (offset + scale). Downside: internal
  API, so it must be rebuilt per Plasma release. Ship like other third-party
  KWin plugins (Better Blur, KDE Rounded Corners): source on GitHub plus a
  Fedora COPR / Arch AUR package.
- Fallback if KWin fails: a GNOME Shell extension (JS). Mutter likely maps
  Wayland pointer input through the window actor's transform, so a scaled
  window may be clickable for free (unverified; Xwayland apps probably not).
- Long term: ask KWin upstream for a way to set a window's input transform.

The 100%/200% scaling workaround was only needed for nested Wayfire. A KWin
plugin runs in the real session at any KDE scale (logical coordinates); the
"lay out at exactly 1x/2x" text trick needs rethinking for fractional scales.
Installed KWin is **6.6.4** (the 6.7 source above was for reading only).

Mapping of the three window states: full size = plugin does nothing;
partially shrunk (dragging) = follow KWin's own interactive move
(`Window::interactiveMoveResizeStepped`) and scale around the cursor with the
same curve; parked (~15%) = visual scale + real resize + input rerouting.

Step 1 is the feasibility test in `kwin/` (see Files); not built yet.

On release the app is also really resized (down to a phone-like width), so
web pages reflow via CSS media queries; the rest of the shrink is visual.

## Environment
- Fedora 44 KDE (aarch64) in a VMware Fusion VM on an Apple Silicon Mac.
  VMware SVGA3D virtual GPU.
- User: scottjenson. Project lives at ~/WideMonitorUX in the VM. The folder was
  renamed from ~/edge-shrink; the plugin inside is still called edge-shrink
  (`src/edge-shrink.cpp`, `libedgeshrink.so`). The user may rename it later.
- Wayfire 0.10.1 from Fedora packages (`wayfire`, `wayfire-devel`), wlroots 0.19.
  No need to build Wayfire from source.
- Build also needs `glm-devel` (Wayfire's headers include GLM but wayfire-devel
  doesn't depend on it); meson.build checks for it at setup time. Installed.
- Display: VMware "Use full resolution for Retina display" is on, and KDE is
  at 100% scale (tiny). The nested Wayfire runs at 2560x1440, scale 2
  (`[output:WL-1]` in wayfire-test.ini), so it is sharp and normal size.
  Nested Wayfire can't tell KDE it is 2x, so KDE at 200% would double it.
- Wayfire is tested **nested**: run as a window inside the KDE Plasma session.
  It must be launched from the VM's graphical session (Konsole in the VM window),
  not from an SSH shell. Crashes only close the nested window.
- The user edits in VS Code, connected into the VM.
- Wayfire 0.10.1 source, for checking internals (not in the repo):
  `dnf download --source wayfire`, then `rpm2cpio wayfire-*.src.rpm | cpio -idm`
  and untar `wayfire-0.10.1.tar.gz`. Most relevant:
  `plugins/common/move-drag-interface.cpp`, `plugins/single_plugins/move.cpp`,
  `src/view/view-3d.cpp` (view_2d_transformer_t).

## Git / GitHub
- Repo: https://github.com/scottjenson/WideMonitorUX (**public**), branch `main`,
  remote `origin` over HTTPS. `gh` is installed and logged in; it is the git
  credential helper, so `git push` works without prompts.
- Local git identity (repo-only config): Scott Jenson <scott@jenson.org>.
- The user wants work committed and pushed so nothing is lost. Commit when a
  change is done and working; ask before pushing anything unusual.
- `build/` is ignored.

## Files
- `meson.build`: builds `build/libedgeshrink.so`. Links the static library
  `libwayfire-move-drag-interface.a` (shipped in Fedora's wayfire-devel).
- `src/edge-shrink.cpp`: the plugin (the only source file).
- `test/breakpoints.html`: test page whose color/label change at widths
  1200/800/600/500 px and that shows its inner size. Open in Firefox inside the
  nested session: file:///home/scottjenson/WideMonitorUX/test/breakpoints.html
- `wayfire-test.ini`: minimal test config. Loads the plugin by absolute path
  (/home/scottjenson/WideMonitorUX/build/libedgeshrink.so), uses `<alt> BTN_LEFT`
  for move (KDE grabs Super), and sets `enable_snap = false` so edge snapping
  doesn't fight the effect. Alt+Enter opens another Konsole. The `plugins =`
  list uses `\` line continuations: to disable the plugin, remove the `\` on
  the `place` line as well as commenting out the plugin line.
- `kwin/`: KWin feasibility test plugin (`main.cpp`, `metadata.json`,
  `CMakeLists.txt`, separate CMake build in `kwin/build/`, not installed).
  Alt+Shift+S shrinks the active window to half size (anchored top-left) or
  restores it, to test whether clicks/scroll/hover land correctly. Needs
  `kwin-devel` and `extra-cmake-modules` (not yet installed). Meant to run
  in a nested KWin (`kwin_wayland --scale 2 ...`) with QT_PLUGIN_PATH
  pointing at the build folder.

## Build and run
    meson setup build        # once (delete build/ and redo if the folder moves)
    meson compile -C build
    wayfire -c ~/WideMonitorUX/wayfire-test.ini   # from Konsole in the VM window

Success check: Wayfire's output includes `edge-shrink: plugin loaded`.
To let the agent read the log, run with
`2>&1 | tee ~/WideMonitorUX/wayfire.log` (the file is git-ignored).
Wayfire only loads the .so at startup: restart the nested session after
rebuilding.

## How the plugin works (src/edge-shrink.cpp)
It does not move windows itself. It hooks into the drag helper shared with
Wayfire's built-in `move` plugin
(`wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper`) and
listens to `drag_motion_signal` and `drag_done_signal`.

**During a drag** (`on_drag_motion`, `drag_scale_t`):
- On the first motion it adds a `drag_scale_t` transformer to the dragged view
  at z = TRANSFORMER_HIGHLEVEL + 1, just above the helper's own transform
  (TRANSFORMER_HIGHLEVEL - 1), which positions the window around the cursor.
- `drag_scale_t` scales that around the cursor (`anchor`) by `scale`,
  instantly. It does NOT use `drag_helper->set_scale`, whose 300 ms animation
  made the window lag the cursor; the helper's own factor stays 1.0.
- Scale = min(edge-distance curve `scale_for_position`, largest scale at which
  the window still fits between the screen edges when scaled around the
  cursor), floored at `min_scale`. Tuning: `zone_width = 400` px,
  `min_scale = 0.15`.
- Drawing quality: both the drag transform and the parked-window transform
  (`smooth_2d_t`, a `view_2d_transformer_t` subclass) draw through
  `smooth_scaler_t`, which halves the texture into offscreen buffers (exact
  2x2 averages, like a mipmap) until the last step is between 1/2 and 1, then
  draws that with bilinear. Plain bilinear below 1/2 skips pixels and makes
  text look dirty. Halving uses raw wlroots passes
  (`wlr_renderer_begin_buffer_pass`), like Wayfire's `render_buffer_t::blit`.
- As a fallback, only when the floor is hit, it slides the window back on screen
  horizontally (`shift_onto_screen`); then the cursor detaches from the grab spot.
- Smoothness: draws at fractional coordinates (`wlr_fbox`), and when the
  surface texture is directly available (zero-copy) draws it itself instead of
  rendering the helper into a temporary buffer.
- While no drag is active (`drag->view` is null) the transformer is a no-op.

**On release** (`on_drag_done`, `place_after_drop`, `show_at`):
- Scales are relative to the window's *original* size: `leftover_scale` =
  visual 2D scale × (current width / original width).
- The window stays exactly where and as large as it was drawn at release. That
  target (`resize_state_t::shown`, the window geometry without client-side
  shadows, output-local) and which edge to pin to are stored on the view as
  `resize_state_t` custom data, together with its original size.
- Real resize, chosen for text quality: if the app can lay out at exactly 1x
  or 2x the shown size (≥ `min_layout_width` (400) and ≥ the app's
  `get_min_size()`), it does, and `shown` is snapped to exactly 1/ratio
  (`resize_state_t::ratio`, `layout`). The drawn size including client-side
  shadows is kept divisible by the ratio, and once the app has committed that
  size `show_at` uses the exact scale and puts the drawn corner on a whole
  pixel. At 1/2, bilinear filtering averages exact 2x2 blocks; 1/3 would just
  pick every third pixel, so only 1 and 2 are used. Too small for either (icon
  sizes): the smallest size that keeps the shape and meets both minimums,
  capped at the original size (Firefox declares 500x120, so 500 px wide).
  The view_2d transform draws into a whole-pixel box (`get_bbox_for_node`
  floors/ceils), which is why exact sizes matter.
- `show_at` fits the window's *current* geometry into `shown` with a
  `view_2d_transformer_t` named `edge-shrink-scale` (z = TRANSFORMER_2D), which
  scales around the geometry's center and maps input. It uses only geometry,
  never the drawn bounding box, which is stale right after the app commits a
  new size (that caused a "window shrinks to 1/4" bug).
- Positioning uses the 2D transform's translation, which applies instantly.
  `view->move` waits for any unfinished resize (Wayfire transactions), so the
  window would otherwise be drawn at its pre-drag position for a few frames
  (a "jumps to the bottom and back" bug). show_at also moves the view to where
  the translation would be zero.
- `on_geometry_changed` (core `view_geometry_changed_signal`) re-runs show_at
  for any view with state, on every resize or move, so the drawing stays in
  place as the app commits its size and moves land. Skips the dragged view.
- Dropped outside the edge zone (total ≥ 0.99): resize back to the original
  size, `restoring = true`; once it's at scale 1 with zero shift, the
  transform and state are removed.
- Placement runs synchronously in the drag_done handler AND again in a
  `wl_idle_call`, because the move plugin also places the window in its own
  drag_done handler and the handler order isn't fixed.
- Released without any motion: nothing is changed.
- Logs one `edge-shrink:` line per resize request (original size, app
  minimum, scale, requested size).

**Dragging a shrunk window again:** the helper sees it at its leftover size;
`drag_scale_t::scale` is relative to that (target / leftover), so it can grow.

`fini()` removes all transforms and resizes windows back to their original size.

## Wayfire 0.10.1 facts (verified in source)
- `core_drag_t::set_scale` takes a shrink *factor* (2.0 = half size) and
  animates over 300 ms. The helper's `scale_around_grab_t` is private (in the
  .cpp); its bbox is `find_geometry_around(children_size / factor, grab, relative)`.
- `handle_motion` sets the helper's grab position to the cursor before emitting
  `drag_motion_signal`.
- The move plugin calls `set_scale(1.0)` on drag start / output change.
- On release, `handle_input_released` removes the helper transform, sets
  `drag_helper->view = nullptr`, then emits `drag_done_signal` (with
  `grab_position` and each view's `relative_grab`). The move plugin's handler
  calls `adjust_view_on_output`, which places the view using
  `view_bounding_box_up_to(view, "wobbly")` (includes transforms below z 500).
- Signal handlers run in connection order; per-output plugins connect when
  outputs appear, so order vs. our plugin isn't guaranteed.
- Transformer z: higher = outer (applied later). TRANSFORMER_2D = 1,
  HIGHLEVEL = 500, BLUR = 1000.
- `view_2d_transformer_t` has `scale_x/scale_y/translation_x/translation_y/
  angle/alpha`, scales around `toplevel->get_geometry()`'s center, maps input.
  Wrap changes in `get_transformed_node()->begin_transform_update()` /
  `end_transform_update()`.
- `toplevel_view_interface_t::get_geometry()` is the *current* (applied)
  geometry; `toplevel()->pending().geometry` is what's been requested.
  `move`/`resize` only set pending state and schedule a transaction; a size
  change waits for the client to ack/commit (or a timeout), and a move in the
  same transaction waits too. On apply, `view_geometry_changed_signal` fires
  synchronously, before the next frame.
- `view_2d_transformer_t` draws a point at
  `center + (p - center) * scale + translation` (center of get_geometry()).
- Resize-related API: `toplevel_view_interface_t::resize(w, h)` /
  `set_geometry(g)`; `toplevel()->get_min_size()` / `get_max_size()` (0x0 when
  the client declares none); `view_geometry_changed_signal` fires when the
  size actually changes.

## Status (2026-09-26)
Tested by the user in the nested session, all working:
- Windows (Konsole, Firefox) shrink while dragged toward either edge, with the
  grabbed spot staying under the cursor and the window pinned to the edge.
- On release they stay exactly where and as large as drawn, and the app is
  really resized: the breakpoints page reflows ("Phone" at the edge) with no
  visible jump. Dragging back out restores the original size.
- Some remaining roughness may be the nested VM (Wayfire → KDE → VMware → macOS).

## Next steps
0. KDE port (see "KDE plan"): build and run the `kwin/` feasibility test; if
   input works, port drag-to-shrink and parking into a KWin plugin (Wayfire
   version stays as reference); then packaging (COPR) and README/LICENSE.
1. Resize experiment follow-ups (ideas, not agreed):
   - Live resizing during the drag (now release-only; while dragging a resized
     window back out it is upscaled and blurry until release).
   - A setting for laying out larger than the on-screen size; tune
     `min_layout_width`.
2. Public-repo housekeeping, offered but not yet done: MIT `LICENSE`
   (meson.build already says MIT) and a short `README.md`. The user also
   hasn't decided whether to hide their email in commits (GitHub noreply).
3. Port/tune the mapping from the HTML prototype into `scale_for_position`
   (may use a curve and/or top/bottom edges).
4. Ideas mentioned: fade the icon-sized window, or cap it at a fixed icon size.
5. Later: config options via a metadata XML file; multiple monitors (the
   on-screen clamp currently blocks dragging onto another monitor).

## Notes for the agent
- The user is new to Linux/Wayland development: explain Linux-specific steps
  (sudo, dnf, -devel packages, etc.) and give exact commands.
- The user prefers simple options (e.g. HTTPS over SSH) and likes design
  choices talked through before big changes.
- The user runs the nested Wayfire and reports how it feels; the agent can
  build but can't see or interact with the nested session. `sudo` needs the
  user's password, so the user runs installs.
- Linux paths are case-sensitive.
