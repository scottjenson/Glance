#!/bin/bash
# Loads the edge-shrink effect from kwin/build into the real Plasma session,
# or stops doing so. Takes effect at the next login (log out and back in).
#
#   kwin/use-in-session.sh on    # KWin looks for effects in kwin/build too
#   kwin/use-in-session.sh off   # back to normal
#
# It adds (or removes) a systemd drop-in for KWin's user service, which sets
# QT_PLUGIN_PATH for KWin only. No sudo needed. After rebuilding, log out and
# back in to load the new version. If the desktop doesn't come up, run
# "off" from an SSH session (or VS Code) and log in again.

DROPIN_DIR="$HOME/.config/systemd/user/plasma-kwin_wayland.service.d"
DROPIN="$DROPIN_DIR/edge-shrink.conf"
BUILD="$(cd "$(dirname "$0")" && pwd)/build/bin"

case "$1" in
on)
    if [ ! -f "$BUILD/kwin/effects/plugins/edgeshrink.so" ]; then
        echo "Not built yet: $BUILD/kwin/effects/plugins/edgeshrink.so" >&2
        exit 1
    fi
    mkdir -p "$DROPIN_DIR"
    cat > "$DROPIN" <<EOF
# Added by WideMonitorUX/kwin/use-in-session.sh; remove with "off".
[Service]
Environment=QT_PLUGIN_PATH=$BUILD
EOF
    systemctl --user daemon-reload
    echo "On. Log out and back in to load edge-shrink."
    ;;
off)
    rm -f "$DROPIN"
    rmdir "$DROPIN_DIR" 2>/dev/null
    systemctl --user daemon-reload
    echo "Off. Log out and back in to unload edge-shrink."
    ;;
*)
    echo "Usage: $0 on|off" >&2
    exit 1
    ;;
esac
