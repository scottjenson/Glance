# Testing

## Headless (the agent can run these)
Load check:

    XDG_RUNTIME_DIR=/run/user/1000 QT_PLUGIN_PATH=$PWD/kwin/build/bin \
    QT_FORCE_STDERR_LOGGING=1 kwin_wayland --virtual --socket es-check \
    --exit-with-session "sleep 3"

Screenshot loop: a virtual KWin on its own D-Bus, with windows and
Spectacle; then look at or measure the PNG. Use big windows: small ones
near the centre hide clipping bugs. `GLANCE_TEST_MAP=1` opens the Alt+Tab
map 3 s after loading (`=N`: after N s).

    GLANCE_TEST_MAP=1 XDG_RUNTIME_DIR=/run/user/1000 \
    QT_PLUGIN_PATH=$PWD/kwin/build/bin dbus-run-session -- kwin_wayland \
    --virtual --width 4004 --height 1630 --scale 1.5 --socket glance-test \
    --exit-with-session "sh -c 'konsole -qwindowgeometry 3900x1500+50+50 &
    sleep 3.6; spectacle -b -n -f -o <file>.png'"

For more steps, put them in a script run by `--exit-with-session "sh script.sh"`:
- KWin scripts over the session's D-Bus: write a .js file, then
  `id=$(qdbus-qt6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript file.js name)`
  and `qdbus-qt6 org.kde.KWin /Scripting/Script$id org.kde.kwin.Script.run`.
  `w.minimized = true` parks a window (minimize = park); `console.info(...)`
  prints window info (resourceClass, desktopFileName, frameGeometry) to
  the log. Works on the real session too (its DBUS_SESSION_BUS_ADDRESS),
  read-only scripts only.
- `konsole -p tabtitle=...` gives windows fixed titles.
- glance-clip needs `QT_QPA_PLATFORMTHEME=kde` for its yellow title bar.

Input can't be injected (no fake-input tool): drags, hover and keys need
the user.

## Crash stacks
`coredumpctl list`, then
`DEBUGINFOD_URLS=https://debuginfod.fedoraproject.org/ coredumpctl debug <pid> --debugger-arguments="-batch -ex 'thread apply all bt 20'"`
(symbols download on demand).

## Smoke test (the user, after bigger changes, ~5 minutes)
1. Drag a window to each edge: it shrinks; drop it in a stash and in
   parking. Drag it back to main: full size again.
2. Meta+Left/Right through all places; Meta+Up, Meta+Down.
3. Use a stashed window: hover, tooltip, text selection, scroll,
   right-click menu, title-bar buttons.
4. Parking icon: hover preview, click goes to the app, drag moves it;
   Meta+wheel over it sizes the preview.
5. Minimize a window: it parks.
6. Meta+drag with a pause (snaps); Meta+wheel on a stashed window.
7. Meta+double-click (declutter), again (undo).
8. Alt+Tab: tap, and hold for the map.
9. Drop text on the desktop, and Meta+C: clips appear; drag a clip into
   a text editor.
10. Focus ring follows Meta+Alt+arrows, with the bounce.
