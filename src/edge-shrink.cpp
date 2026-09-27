// edge-shrink: a Wayfire plugin that scales a window down as it is dragged
// toward the left or right edge of the screen.
//
// Rather than moving windows itself, it hooks into the drag helper that
// Wayfire's built-in "move" plugin uses, and stacks its own transform on top
// of the dragged window to shrink it around the cursor. A window dropped while
// shrunk stays shrunk.
//
// On release the app is also really resized (down to a phone-like width), so
// web pages reflow via CSS media queries; the rest of the shrink is visual.

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// How far a box spanning [x, x + width) must move horizontally to lie inside
// `screen`. A box wider than the screen keeps its left edge visible.
static double shift_onto_screen(double x, double width, wf::geometry_t screen)
{
    // Smallest shift that brings the left edge back on screen, and the
    // largest shift that keeps the right edge on screen.
    double min_shift = screen.x - x;
    double max_shift = (screen.x + screen.width) - (x + width);
    if (min_shift > max_shift)
    {
        return min_shift;
    }

    return std::clamp(0.0, min_shift, max_shift);
}

// Draws a window's texture shrunk, with less of the "dirty" look of plain
// bilinear filtering. Bilinear only blends the 4 pixels nearest each sample, so
// below half size it skips pixels and text breaks up. Halving exactly, though,
// averages each 2x2 block perfectly. So the texture is first halved as often
// as needed into offscreen buffers (like a mipmap), and only the last step,
// between 1/2 and 1, uses a free scale.
class smooth_scaler_t
{
    std::vector<wf::auxilliary_buffer_t> halves;

    // Copy `src_box` of `src` into all of `dst`, scaled to fit.
    static bool copy(wlr_texture *src, wlr_fbox src_box, wf::auxilliary_buffer_t& dst,
        wf::dimensions_t size)
    {
        if (dst.allocate(size) == wf::buffer_reallocation_result_t::FAILED)
        {
            return false;
        }

        auto pass = wlr_renderer_begin_buffer_pass(wf::get_core().renderer,
            dst.get_buffer(), NULL);
        if (!pass)
        {
            return false;
        }

        wlr_render_texture_options opts{};
        opts.texture     = src;
        opts.src_box     = src_box;
        opts.dst_box     = {0, 0, size.width, size.height};
        opts.filter_mode = WLR_SCALE_FILTER_BILINEAR;
        opts.blend_mode  = WLR_RENDER_BLEND_MODE_NONE;
        wlr_render_pass_add_texture(pass, &opts);
        return wlr_render_pass_submit(pass);
    }

  public:
    void draw(const wf::scene::render_instruction_t& data, wf::texture_t tex,
        wlr_fbox box, float alpha = 1.0)
    {
        tex.filter_mode = WLR_SCALE_FILTER_BILINEAR;
        wlr_fbox src = tex.source_box.value_or(wlr_fbox{0, 0,
            1.0 * tex.texture->width, 1.0 * tex.texture->height});

        // Size on screen, in real pixels.
        double want_w = box.width * data.target.scale;
        double want_h = box.height * data.target.scale;

        size_t steps = 0;
        if (tex.transform == WL_OUTPUT_TRANSFORM_NORMAL)
        {
            while ((src.width / 2 >= want_w) && (src.height / 2 >= want_h) &&
                   (src.width >= 4) && (src.height >= 4))
            {
                if (halves.size() <= steps)
                {
                    halves.emplace_back();
                }

                wf::dimensions_t half = {
                    (int)std::round(src.width / 2), (int)std::round(src.height / 2)
                };
                if (!copy(tex.texture, src, halves[steps], half))
                {
                    break;
                }

                tex = wf::texture_t{halves[steps].get_texture()};
                tex.filter_mode = WLR_SCALE_FILTER_BILINEAR;
                src = {0, 0, 1.0 * half.width, 1.0 * half.height};
                steps++;
            }
        }

        // Free buffers no longer needed (e.g. after growing again).
        halves.resize(steps);
        data.pass->add_texture(tex, data.target, box, data.damage, alpha);
    }
};

// A transformer stacked on top of the drag helper's own transform, which
// positions the window around the cursor (at its drawn size, i.e. including
// any shrink left over from an earlier drop).
//
// This node scales that by `scale` around `anchor` (the cursor), immediately
// rather than with the helper's 300 ms animation, so the grabbed spot stays
// under the cursor. As a fallback, for when even the smallest allowed size
// doesn't fit, it slides the window back inside `screen` horizontally.
class drag_scale_t : public wf::scene::transformer_base_node_t
{
  public:
    // The drag we belong to. While no drag is active this node does nothing,
    // so Wayfire's drop logic sees the window's real position.
    wf::move_drag::core_drag_t *drag;

    // Output bounds, anchor and scale, in output-layout coordinates (the same
    // space the drag helper's transform works in).
    wf::geometry_t screen = {0, 0, 0, 0};
    wf::point_t anchor    = {0, 0};
    double scale = 1.0;

    drag_scale_t(wf::move_drag::core_drag_t *drag) :
        transformer_base_node_t(false), drag(drag)
    {}

    std::string stringify() const override
    {
        return "edge-shrink";
    }

    bool active() const
    {
        return drag->view && (screen.width > 0);
    }

    // Where the window is drawn: the children's box scaled around the anchor
    // and shifted back on screen. Kept fractional, so the window glides
    // instead of its edges snapping between whole pixels independently.
    wlr_fbox exact_box()
    {
        auto box = get_children_bounding_box();
        if (!active())
        {
            return {1.0 * box.x, 1.0 * box.y, 1.0 * box.width, 1.0 * box.height};
        }

        double x = anchor.x + (box.x - anchor.x) * scale;
        double y = anchor.y + (box.y - anchor.y) * scale;
        double w = box.width * scale;
        double h = box.height * scale;
        x += shift_onto_screen(x, w, screen);
        return {x, y, w, h};
    }

    double x_offset()
    {
        if (!active())
        {
            return 0;
        }

        auto box = get_children_bounding_box();
        double x = anchor.x + (box.x - anchor.x) * scale;
        return shift_onto_screen(x, box.width * scale, screen);
    }

    wf::pointf_t to_local(const wf::pointf_t& point) override
    {
        if (!active())
        {
            return point;
        }

        return {
            anchor.x + (point.x - x_offset() - anchor.x) / scale,
            anchor.y + (point.y - anchor.y) / scale,
        };
    }

    wf::pointf_t to_global(const wf::pointf_t& point) override
    {
        if (!active())
        {
            return point;
        }

        return {
            anchor.x + (point.x - anchor.x) * scale + x_offset(),
            anchor.y + (point.y - anchor.y) * scale,
        };
    }

    // Whole pixels enclosing exact_box(), for damage tracking.
    wf::geometry_t get_bounding_box() override
    {
        auto box = exact_box();
        int x1   = std::floor(box.x);
        int y1   = std::floor(box.y);
        int x2   = std::ceil(box.x + box.width);
        int y2   = std::ceil(box.y + box.height);
        return {x1, y1, x2 - x1, y2 - y1};
    }

    class render_instance_t :
        public wf::scene::transformer_render_instance_t<drag_scale_t>
    {
        // The drag helper below us only draws the window's surface texture
        // around the cursor. When that texture is directly available, draw it
        // ourselves and skip rendering the helper into a temporary buffer:
        // that saves a full-window copy every frame.
        std::optional<wf::texture_t> direct_texture()
        {
            auto& helper = self->get_children();
            if (helper.size() != 1)
            {
                return {};
            }

            auto inner = helper.front()->get_children();
            if (inner.size() != 1)
            {
                return {};
            }

            if (auto zcopy =
                    dynamic_cast<wf::scene::zero_copy_texturable_node_t*>(inner.front().get()))
            {
                return zcopy->to_texture();
            }

            return {};
        }

        smooth_scaler_t scaler;

      public:
        using transformer_render_instance_t::transformer_render_instance_t;

        void transform_damage_region(wf::region_t& region) override
        {
            region |= self->get_bounding_box();
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            auto tex = direct_texture();
            if (tex)
            {
                self->release_buffers();
            } else
            {
                tex = this->get_texture(data.target.scale);
            }

            scaler.draw(data, *tex, self->exact_box());
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this,
            push_damage, shown_on));
    }
};

// Wayfire's 2D transform (which scales the window and maps input onto it),
// drawn with smooth_scaler_t instead of plain bilinear filtering. Used for
// windows parked shrunk.
class smooth_2d_t : public wf::scene::view_2d_transformer_t
{
  public:
    using view_2d_transformer_t::view_2d_transformer_t;

    class render_instance_t :
        public wf::scene::transformer_render_instance_t<smooth_2d_t>
    {
        smooth_scaler_t scaler;

      public:
        using transformer_render_instance_t::transformer_render_instance_t;

        void transform_damage_region(wf::region_t& damage) override
        {
            auto copy = damage;
            damage.clear();
            for (auto& box : copy)
            {
                damage |= wf::get_bbox_for_node(self, wlr_box_from_pixman_box(box));
            }
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            auto box = self->get_bounding_box();
            scaler.draw(data, get_texture(data.target.scale),
                {1.0 * box.x, 1.0 * box.y, 1.0 * box.width, 1.0 * box.height},
                self->get_alpha());
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this,
            push_damage, shown_on));
    }
};

// Kept on a window the plugin has really resized.
struct resize_state_t : public wf::custom_data_t
{
    // The window's size before we first resized it.
    wf::dimensions_t original;
    // Where the window proper (without shadows) should appear, output-local.
    // When the app commits a new size, it is scaled to fit this box again.
    wlr_fbox shown;
    // Keep the right edge of `shown` fixed rather than the left one.
    bool pin_right = false;
    // We asked the app to go back to `original`; forget this state once it has.
    bool restoring = false;
    // When nonzero, the app was asked to lay out at `layout`, exactly `ratio`
    // times the size it is shown at (1 or 2). Once it has, it is drawn at
    // exactly 1/ratio, on whole pixels, which keeps text as clean as possible.
    int ratio = 0;
    wf::dimensions_t layout = {0, 0};
};

class edge_shrink_plugin : public wf::plugin_interface_t
{
    // --- Tuning knobs (later these can become config options) ---
    // Distance from the left/right edge, in pixels, where shrinking begins.
    const double zone_width = 400.0;
    // Window size at the very edge of the screen (1.0 = full size).
    const double min_scale = 0.15;
    // On release, the app is resized no narrower than this (keeping its
    // shape), so web pages switch to their phone layout. The rest is visual.
    const double min_layout_width = 400.0;

    // A window dropped while shrunk keeps a view_2d_transformer_t with this
    // name. It scales around the window's center and also maps input, so the
    // small window stays clickable.
    const std::string shrink_name = "edge-shrink-scale";

    std::shared_ptr<wf::scene::view_2d_transformer_t> get_shrink(wayfire_toplevel_view view)
    {
        return view->get_transformed_node()->
               get_transformer<wf::scene::view_2d_transformer_t>(shrink_name);
    }

    // How large a window is drawn relative to its original size, as left by
    // an earlier drop (1.0 if none): its visual scale times its real resize.
    double leftover_scale(wayfire_toplevel_view view)
    {
        auto tr = get_shrink(view);
        double scale = tr ? tr->scale_x : 1.0;
        if (auto state = view->get_data<resize_state_t>())
        {
            scale *= 1.0 * view->get_geometry().width / state->original.width;
        }

        return scale;
    }

    // Runs our drop handling after the move plugin has placed the window.
    wf::wl_idle_call idle_place;

    // Shared drag state, owned jointly with the move plugin.
    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag_helper;

    // The window currently being dragged, and our scaling transform on it.
    wayfire_toplevel_view clamped_view = nullptr;
    std::shared_ptr<drag_scale_t> clamp;

    void attach_clamp(wayfire_toplevel_view view)
    {
        detach_clamp();
        clamp = std::make_shared<drag_scale_t>(drag_helper.get());
        // Just above the drag helper's transform (TRANSFORMER_HIGHLEVEL - 1),
        // so we act on the window as the helper has positioned it.
        view->get_transformed_node()->add_transformer(clamp,
            wf::TRANSFORMER_HIGHLEVEL + 1, "edge-shrink");
        clamped_view = view;
    }

    void detach_clamp()
    {
        if (clamped_view && clamp)
        {
            clamped_view->get_transformed_node()->rem_transformer(clamp);
        }

        clamped_view = nullptr;
        clamp = nullptr;
    }

    // Map the pointer position to a window scale.
    double scale_for_position(wf::point_t pos, wf::geometry_t screen)
    {
        double to_left  = pos.x - screen.x;
        double to_right = (screen.x + screen.width) - pos.x;
        double distance = std::max(0.0, std::min(to_left, to_right));

        if (distance >= zone_width)
        {
            return 1.0;
        }

        double t = distance / zone_width; // 0 at the edge, 1 at the zone boundary
        return min_scale + (1.0 - min_scale) * t;
    }

    // Called on every pointer movement during a window drag.
    wf::signal::connection_t<wf::move_drag::drag_motion_signal> on_drag_motion =
        [this] (wf::move_drag::drag_motion_signal *ev)
    {
        auto output = drag_helper->current_output;
        if (!drag_helper->view || !output)
        {
            return;
        }

        if (clamped_view != drag_helper->view)
        {
            attach_clamp(drag_helper->view);
        }

        auto screen = output->get_layout_geometry();
        auto cursor = ev->current_position;

        // The window as the drag helper draws it: at its leftover size, with
        // the grabbed spot under the cursor.
        auto box = clamp->get_children_bounding_box();
        double leftover = leftover_scale(clamped_view);

        // Our scale is applied on top of the leftover one.
        double scale = scale_for_position(cursor, screen) / leftover;

        // Shrink further if needed so that, scaled around the cursor, the
        // window still fits between the screen edges. Its edge then rests
        // against the screen edge while the grabbed spot stays under the cursor.
        int box_right = box.x + box.width;
        if (cursor.x > box.x)
        {
            scale = std::min(scale, 1.0 * (cursor.x - screen.x) / (cursor.x - box.x));
        }

        if (box_right > cursor.x)
        {
            scale = std::min(scale,
                1.0 * (screen.x + screen.width - cursor.x) / (box_right - cursor.x));
        }

        scale = std::max(scale, min_scale / leftover);

        auto node = clamped_view->get_transformed_node();
        node->begin_transform_update();
        clamp->screen = screen;
        clamp->anchor = cursor;
        clamp->scale  = scale;
        node->end_transform_update();
    };

    wf::signal::connection_t<wf::move_drag::drag_done_signal> on_drag_done =
        [this] (wf::move_drag::drag_done_signal *ev)
    {
        if (!clamped_view || !clamp)
        {
            // Released without any motion: nothing changed.
            return;
        }

        // The window's total scale at release, relative to its original size.
        double scale = leftover_scale(clamped_view) * clamp->scale;
        detach_clamp();

        if (!ev->main_view || !ev->focused_output)
        {
            return;
        }

        wf::pointf_t relative = {0.5, 0.5};
        for (auto& v : ev->all_views)
        {
            if (v.view == ev->main_view)
            {
                relative = v.relative_grab;
            }
        }

        // The move plugin also places the window, in its own drag_done
        // handler, which may run before or after this one. Place it now, so
        // no frame shows the window at full size, and once more after the
        // move plugin is done in case it ran after us.
        auto view   = ev->main_view;
        auto screen = ev->focused_output->get_layout_geometry();
        auto grab   = ev->grab_position;
        place_after_drop(view, grab, relative, scale, screen);

        std::weak_ptr<wf::view_interface_t> weak = view->weak_from_this();
        idle_place.run_once([=] ()
        {
            if (auto v = weak.lock())
            {
                place_after_drop(wf::toplevel_cast(wayfire_view{v.get()}),
                    grab, relative, scale, screen);
            }
        });
    };

    // Leave the window exactly where and as large as it was drawn at the
    // moment of release: `total` times its original size, positioned around
    // the grab point, and pushed back inside the screen the same way
    // drag_scale_t did. When shrunk, also ask the app to really resize.
    void place_after_drop(wayfire_toplevel_view view, wf::point_t grab,
        wf::pointf_t relative, double total, wf::geometry_t screen)
    {
        if (!view || !view->is_mapped() || view->pending_fullscreen() ||
            view->pending_tiled_edges())
        {
            return;
        }

        // Measure the window at its current (unscaled) size. If it is already
        // shrunk, the scale transform's children are the unscaled window.
        auto tr    = get_shrink(view);
        auto box   = tr ? tr->get_children_bounding_box() :
            view->get_transformed_node()->get_bounding_box();
        auto geom  = view->get_geometry();
        auto state = view->get_data<resize_state_t>();
        wf::dimensions_t original = state ? state->original : wf::dimensions(geom);

        // The scale relative to the current size.
        double scale = total * original.width / geom.width;

        // Where the window was drawn at release, output-local.
        int width  = std::floor(box.width * scale);
        int height = std::floor(box.height * scale);
        wf::geometry_t shown = {
            grab.x - (int)std::floor(relative.x * width),
            grab.y - (int)std::floor(relative.y * height),
            width, height,
        };
        shown.x += std::round(shift_onto_screen(shown.x, shown.width, screen));
        shown.x -= screen.x;
        shown.y -= screen.y;
        bool pin_right = shown.x + shown.width / 2 > screen.width / 2;

        // The same, for the window proper: `box` may include client-side
        // shadows, which stay the same number of pixels when the app resizes,
        // so everything after this works with the window's geometry.
        wlr_fbox target = {
            shown.x + (geom.x - box.x) * scale,
            shown.y + (geom.y - box.y) * scale,
            geom.width * scale,
            geom.height * scale,
        };

        auto pending = wf::dimensions(view->toplevel()->pending().geometry);

        if (!state)
        {
            view->store_data(std::make_unique<resize_state_t>());
            state = view->get_data<resize_state_t>();
            state->original = original;
        }

        state->shown     = target;
        state->pin_right = pin_right;

        if (total >= 0.99)
        {
            // Dropped outside the edge zone: back to the original size. The
            // state is forgotten once the window is back in place.
            if ((wf::dimensions(geom) != original) && (pending != original))
            {
                LOGI("edge-shrink: ", view->get_app_id(), " restoring ",
                    original.width, "x", original.height);
                view->resize(original.width, original.height);
            }

            state->restoring = true;
            state->ratio     = 0;
            show_at(view, target, pin_right);
            return;
        }

        // Shrunk. Scaling text by exactly 1 or 1/2 keeps it clean (at 1/2,
        // each screen pixel averages exactly a 2x2 block of the app's pixels);
        // other factors blur it unevenly. So if the app can lay out at 1x or
        // 2x the shown size, respecting our minimum width and the app's
        // minimum size, it does, and the shown size is snapped to match.
        auto min = view->toplevel()->get_min_size();
        double min_width = std::max(min_layout_width, 1.0 * min.width);
        int shown_w = std::round(target.width);
        int shown_h = std::round(target.height);

        int ratio = 0;
        for (int n : {1, 2})
        {
            if ((n * shown_w >= min_width) && (n * shown_h >= min.height))
            {
                ratio = n;
                break;
            }
        }

        wf::dimensions_t layout;
        if (ratio)
        {
            // What is drawn includes any client-side shadows, so make that
            // whole size divisible by the ratio, to land on whole pixels.
            int pad_w = box.width - geom.width;
            int pad_h = box.height - geom.height;
            layout = {ratio * shown_w, ratio * shown_h};
            layout.width  += (ratio - (layout.width + pad_w) % ratio) % ratio;
            layout.height += (ratio - (layout.height + pad_h) % ratio) % ratio;

            // Snap the shown box to exactly 1/ratio of that, keeping the
            // pinned edge and vertical center.
            double w = 1.0 * layout.width / ratio;
            double h = 1.0 * layout.height / ratio;
            if (pin_right)
            {
                target.x += target.width - w;
            }

            target.y     += (target.height - h) / 2.0;
            target.width  = w;
            target.height = h;
            state->shown  = target;
        } else
        {
            // Too small for either: lay the app out at the smallest size that
            // keeps its shape and respects both minimums.
            double k = std::max({total,
                min_width / original.width,
                1.0 * min.height / original.height});
            k = std::min(k, 1.0);
            layout = {
                (int)std::round(original.width * k),
                (int)std::round(original.height * k),
            };
        }

        state->ratio  = ratio;
        state->layout = layout;

        if (pending != layout)
        {
            LOGI("edge-shrink: ", view->get_app_id(), " original ",
                original.width, "x", original.height, ", app minimum ",
                min.width, "x", min.height, ", scale ", total,
                ", resizing to ", layout.width, "x", layout.height,
                ratio ? ", shown at exactly 1/" + std::to_string(ratio) :
                std::string(", shown at a free scale"));
            view->resize(layout.width, layout.height);
        }

        state->restoring = false;

        // Show it at the right size now; once the app has committed its new
        // size, on_geometry_changed fits it into `shown` again.
        show_at(view, target, pin_right);
    }

    // Scale the window, at whatever size it currently is, to fit `shown`
    // (the window proper, output-local), keeping its shape. It is pinned to
    // the left or right edge of `shown` and centered vertically.
    //
    // Only the window's geometry is used: right after the app commits a new
    // size, the drawn bounding box is still stale.
    //
    // Moving a window waits for any resize the app hasn't finished yet, and
    // until then it would be drawn at its old position. So the transform's
    // translation places it exactly, right away, wherever the window really
    // is; the window is also moved there, and once that lands (see
    // on_geometry_changed) the translation drops back to zero.
    void show_at(wayfire_toplevel_view view, wlr_fbox shown, bool pin_right)
    {
        auto node = view->get_transformed_node();
        auto tr   = get_shrink(view);
        auto geom = view->get_geometry();
        if ((geom.width <= 0) || (geom.height <= 0))
        {
            return;
        }

        // Laid out at the size we asked for, for an exact ratio?
        auto state = view->get_data<resize_state_t>();
        bool exact = state && state->ratio && (wf::dimensions(geom) == state->layout);

        double scale = exact ? 1.0 / state->ratio :
            std::min(shown.width / geom.width, shown.height / geom.height);
        if (std::abs(scale - 1.0) < 0.01)
        {
            scale = 1.0;
        }

        // Where the scaled window's top-left corner goes.
        double sx = pin_right ? shown.x + shown.width - geom.width * scale : shown.x;
        double sy = shown.y + (shown.height - geom.height * scale) / 2.0;

        if (exact)
        {
            // Put the corner of what is drawn (including shadows) on a whole
            // pixel, so each screen pixel covers exactly `ratio` x `ratio`
            // of the app's pixels.
            auto drawn = tr ? tr->get_children_bounding_box() : node->get_bounding_box();
            double dx  = sx + (drawn.x - geom.x) * scale;
            double dy  = sy + (drawn.y - geom.y) * scale;
            sx += std::round(dx) - dx;
            sy += std::round(dy) - dy;
        }

        // The 2D transform scales around the window's center, which moves
        // the corner inward by half the size lost. `tx`, `ty` is what is
        // left to shift from where the window is now.
        double tx = sx - geom.x - geom.width * (1.0 - scale) / 2.0;
        double ty = sy - geom.y - geom.height * (1.0 - scale) / 2.0;

        // Where the window should really be, for no shift at all.
        int x = std::round(geom.x + tx);
        int y = std::round(geom.y + ty);

        if ((scale == 1.0) && (std::abs(tx) < 0.5) && (std::abs(ty) < 0.5))
        {
            // Drawn at its real size and place: no transform needed, so it
            // stays crisp.
            if (tr)
            {
                node->rem_transformer(tr);
            }

            if (state && state->restoring)
            {
                view->erase_data<resize_state_t>();
            }

            return;
        }

        // Reusing an existing transform (rather than removing and re-adding
        // it) avoids a frame at full size.
        if (!tr)
        {
            tr = std::make_shared<smooth_2d_t>(view);
            node->add_transformer(tr, wf::TRANSFORMER_2D, shrink_name);
        }

        node->begin_transform_update();
        tr->scale_x = scale;
        tr->scale_y = scale;
        tr->translation_x = tx;
        tr->translation_y = ty;
        node->end_transform_update();

        auto pending = view->toplevel()->pending().geometry;
        if ((pending.x != x) || (pending.y != y))
        {
            view->move(x, y);
        }
    }

    // The app committed a new size (possibly not the one we asked for), or a
    // move landed: fit the window into the box it should appear in.
    wf::signal::connection_t<wf::view_geometry_changed_signal> on_geometry_changed =
        [this] (wf::view_geometry_changed_signal *ev)
    {
        auto view = ev->view;
        if (!view || (view == clamped_view) || (view == drag_helper->view) ||
            !view->is_mapped())
        {
            return;
        }

        auto state = view->get_data<resize_state_t>();
        if (!state)
        {
            return;
        }

        // Resized or moved: re-fit, so it keeps being drawn in place.
        show_at(view, state->shown, state->pin_right);
    };

  public:
    void init() override
    {
        drag_helper->connect(&on_drag_motion);
        drag_helper->connect(&on_drag_done);
        wf::get_core().connect(&on_geometry_changed);
        LOGI("edge-shrink: plugin loaded");
    }

    void fini() override
    {
        on_drag_motion.disconnect();
        on_drag_done.disconnect();
        on_geometry_changed.disconnect();
        idle_place.disconnect();
        detach_clamp();

        // Return every shrunk window to full size.
        for (auto& v : wf::get_core().get_all_views())
        {
            if (auto view = wf::toplevel_cast(v))
            {
                if (auto tr = get_shrink(view))
                {
                    view->get_transformed_node()->rem_transformer(tr);
                }

                if (auto state = view->get_data<resize_state_t>())
                {
                    view->resize(state->original.width, state->original.height);
                    view->erase_data<resize_state_t>();
                }
            }
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(edge_shrink_plugin);
