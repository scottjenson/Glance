#!/bin/bash
# Opens the breakpoints test page in a separate Firefox, for use inside the
# nested KWin (run it from the inner Konsole). --no-remote and its own
# profile keep it from opening in the desktop's Firefox instead.
PROFILE="${XDG_RUNTIME_DIR:-/tmp}/glance-firefox"
mkdir -p "$PROFILE"
firefox --no-remote --profile "$PROFILE" "file://$(cd "$(dirname "$0")/.." && pwd)/test/breakpoints.html" &
