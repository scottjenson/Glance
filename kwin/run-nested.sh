#!/bin/bash
# Starts a nested KWin (a window inside the current Plasma session) with the
# edge-shrink test plugin loaded from kwin/build, and a Konsole inside it.
# Run from Konsole in the VM window. Output goes to the terminal and to
# ~/WideMonitorUX/kwin.log. More windows: run `konsole &` in the inner Konsole.
cd "$(dirname "$0")"
export QT_PLUGIN_PATH="$PWD/build/bin"
export QT_FORCE_STDERR_LOGGING=1
kwin_wayland --width 1280 --height 800 --scale 2 konsole 2>&1 | tee ../kwin.log
