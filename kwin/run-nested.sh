#!/bin/bash
# Starts a nested KWin (a window inside the current Plasma session) with the
# Glance effect loaded from kwin/build, and a Konsole inside it.
# Run from Konsole in the VM window. Output goes to the terminal and to
# ~/Glance/kwin.log (the previous one is kept as kwin.log.1).
# More windows: run `konsole &` in the inner Konsole; for Firefox, use
# kwin/nested-firefox.sh.
cd "$(dirname "$0")"
export QT_PLUGIN_PATH="$PWD/build/bin"
export QT_FORCE_STDERR_LOGGING=1
# Window size in the desktop's logical pixels. The default fills the VM's
# 2995x1252 screen (at 200%) below the panel; override with e.g.
# WIDTH=1280 HEIGHT=800 ./run-nested.sh
WIDTH=${WIDTH:-2982}
HEIGHT=${HEIGHT:-1090}
# Keep the previous session's log.
[ -f ../kwin.log ] && mv ../kwin.log ../kwin.log.1
kwin_wayland --width "$WIDTH" --height "$HEIGHT" --scale 2 konsole 2>&1 | tee ../kwin.log
