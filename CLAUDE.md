# edge-shrink: Wayfire plugin

## Goal
A window-management experiment: as the user drags a window toward the edge of
the screen, the window scales down (the content visually shrinks, the app is not
resized/reflowed). The interaction was prototyped first in HTML/JavaScript on a
Mac; this project makes it real as a plugin for the Wayfire Wayland compositor.

Wayfire was chosen over KWin effects because its view transformers handle both
rendering and input mapping (clicks land correctly on a scaled window).

## Environment
- Fedora 44 KDE (aarch64) in a VMware Fusion VM on an Apple Silicon Mac.
- User: scottjenson. Project lives at ~/edge-shrink in the VM.
- Wayfire 0.10.1 from Fedora packages (`wayfire`, `wayfire-devel`), wlroots 0.19.
  No need to build Wayfire from source.
- Wayfire is tested **nested**: run as a window inside the KDE Plasma session.
  It must be launched from the VM's graphical session (Konsole in the VM window),
  not from an SSH shell. Crashes only close the nested window.

## Files
- `meson.build`: builds `build/libedgeshrink.so`. Links the static library
  `libwayfire-move-drag-interface.a` (shipped in Fedora's wayfire-devel).
- `src/edge-shrink.cpp`: the plugin.
- `wayfire-test.ini`: minimal test config. Loads the plugin by absolute path
  (/home/scottjenson/edge-shrink/build/libedgeshrink.so), uses `<alt> BTN_LEFT`
  for move (KDE grabs Super), and sets `enable_snap = false` so edge snapping
  doesn't fight the effect. Alt+Enter opens another Konsole.

## Build and run
    meson setup build        # once
    meson compile -C build
    wayfire -c ~/edge-shrink/wayfire-test.ini   # from Konsole in the VM window

Success check: Wayfire's output includes `edge-shrink: plugin loaded`.

## How the current plugin works
It does not move windows itself. It hooks into the drag helper shared with
Wayfire's built-in `move` plugin:
- `wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper`
- Listens to `drag_motion_signal` (pointer position in output-layout coords),
  `drag_focus_output_signal`, and `drag_done_signal`.
- Computes a scale from the pointer's distance to the nearest left/right edge of
  `drag_helper->current_output->get_layout_geometry()` and calls
  `drag_helper->set_scale(1.0 / scale)`.
- Scales the dragged window itself (not via `set_scale`, whose 300 ms
  animation made the window lag the cursor): a `drag_scale_t` transformer
  (z = TRANSFORMER_HIGHLEVEL + 1, above the helper's transform) scales the
  window around the cursor, instantly. Scale = min(edge-distance curve, largest
  scale at which the window still fits between the screen edges), floored at
  `min_scale` (0.15: the user wants windows at the edge to feel like icons). User requirements: the grabbed spot must stay under the cursor,
  the window's edge rests against the screen edge and never goes off screen,
  and shrinking must not require moving the cursor to the very edge. Only if
  the floor is hit does it slide the window back on screen (cursor detaches).
- Smoothness (user reported flicker; plain Wayfire drags in the same nested VM
  are smooth): `drag_scale_t` draws at fractional coordinates (`wlr_fbox`) and,
  when the surface texture is directly available, draws it itself instead of
  rendering the helper into a temporary buffer. On drop, placement runs
  synchronously and again in an idle callback (drag_done handler order vs the
  move plugin isn't fixed), reusing the existing 2D transform, so no frame
  shows the window at full size.
- Release (user decision: "stay shrunk, in place"): on `drag_done_signal` it
  waits for an idle callback (so the move plugin's own placement runs first),
  then moves the window and adds a `view_2d_transformer_t` named
  `edge-shrink-scale` so it stays exactly where and as large as it was drawn at
  release. Dropping outside the edge zone (scale 1.0) removes it. When a shrunk
  window is dragged again, the drag helper's factor is `leftover / target`.

Verified against the Wayfire 0.10.1 source:
- `set_scale` takes a shrink *factor* (2.0 = half size) and animates each change
  over 300 ms, which adds some smoothing/lag.
- During a drag, the move helper renders the view through its private
  `scale_around_grab_t` transformer, anchored at the grab point, so the grabbed
  spot stays under the cursor automatically. The view's real geometry does not
  change until drop.
- The move plugin calls `set_scale(1.0)` when a drag starts or changes outputs.
- On drop, the move plugin places the view at full size around the grab point
  (edge-shrink then overrides this, see above).
- `wf::scene::view_2d_transformer_t` (wayfire/view-transform.hpp) has
  `scale_x/scale_y/translation_x/translation_y/angle/alpha`, scales around the
  view's center, and maps input. Add with
  `view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D, "name")`.
  Wrap attribute changes in `begin_transform_update()` / `end_transform_update()`.

## Status
The plugin compiles and links cleanly against Wayfire 0.10.1 (no code changes
were needed). Building requires `glm-devel` (`sudo dnf install glm-devel`),
which Wayfire's headers use but wayfire-devel doesn't depend on; meson.build
checks for it at setup time. Confirmed working in a nested session (2026-09-26):
the plugin loads, windows shrink while dragged toward the edge, and the
on-screen pinning during drags works. The keep-shrunk-on-release code has not
been tested yet.

## Next steps
1. Build, run nested, confirm the shrink effect during drags.
2. Port the mapping from the HTML prototype into `scale_for_position` (it may use
   a curve and/or top/bottom edges). Tune `zone_width` and `min_scale`.
3. Test the keep-shrunk-on-release behavior (implemented, untested).
4. Possibly replace the 300 ms animated `set_scale` with immediate scaling.
5. Later: expose options through a metadata XML file, handle multiple monitors.

## Notes
- The user is new to Linux/Wayland development; explain Linux-specific steps.
- Linux paths are case-sensitive (`src`, not `Src`).
- The user runs the nested Wayfire and reports how it feels; the agent can build
  but can't see or interact with the nested session.
