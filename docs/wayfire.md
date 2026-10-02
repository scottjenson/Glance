# Wayfire prototype (wayfire/, reference only)

Keeps its old edge-shrink names. Same behaviour as the KWin effect, earlier
version (cursor-driven 400 px zone). Build:
`meson setup wayfire/build && meson compile -C wayfire/build`; run
`wayfire -c ~/Glance/wayfire/wayfire-test.ini` from Konsole in the VM
window. Needs `wayfire`, `wayfire-devel`, `glm-devel` (installed). It worked
nested only with KDE at 100% (it can't tell KDE it is 2x).

Worth porting from it: `smooth_scaler_t`, which halves the texture into
offscreen buffers (exact 2x2 averages, like a mipmap) until the last step is
between 1/2 and 1, then draws with bilinear; plain bilinear below 1/2 skips
pixels and makes text look dirty.

Detailed notes on its internals and Wayfire 0.10.1 facts: CLAUDE.md as of
commit 6f217f6.
