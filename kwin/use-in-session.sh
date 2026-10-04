#!/bin/bash
# Loads the Glance effect from kwin/build into the real Plasma session,
# or stops doing so. Takes effect at the next login (log out and back in).
#
#   kwin/use-in-session.sh on    # KWin looks for effects in kwin/build too
#   kwin/use-in-session.sh off   # back to normal
#
# It adds (or removes) a systemd drop-in for KWin's user service, which sets
# QT_PLUGIN_PATH for KWin only, and an autostart entry for glance-check (a
# dialog at login if KWin didn't load Glance, e.g. after a Plasma update).
# No sudo needed. After rebuilding, log out and
# back in to load the new version. If the desktop doesn't come up, run
# "off" from an SSH session (or VS Code) and log in again.

DROPIN_DIR="$HOME/.config/systemd/user/plasma-kwin_wayland.service.d"
DROPIN="$DROPIN_DIR/glance.conf"
# The drop-in's name before the project was renamed from edge-shrink.
OLD_DROPIN="$DROPIN_DIR/edge-shrink.conf"
BUILD="$(cd "$(dirname "$0")" && pwd)/build/bin"
# Same name as the installed entry, which this one then replaces.
CHECK="$HOME/.config/autostart/org.glance.Check.desktop"

case "$1" in
on)
    if [ ! -f "$BUILD/kwin/effects/plugins/glance.so" ]; then
        echo "Not built yet: $BUILD/kwin/effects/plugins/glance.so" >&2
        exit 1
    fi
    mkdir -p "$DROPIN_DIR"
    rm -f "$OLD_DROPIN"
    cat > "$DROPIN" <<EOF
# Added by Glance's kwin/use-in-session.sh; remove with "off".
[Service]
Environment=QT_PLUGIN_PATH=$BUILD
EOF
    mkdir -p "$(dirname "$CHECK")"
    sed "s|^Exec=.*|Exec=$BUILD/glance-check|" "$BUILD/../org.glance.Check.desktop" > "$CHECK"
    systemctl --user daemon-reload
    echo "On. Log out and back in to load Glance."
    ;;
off)
    rm -f "$DROPIN" "$OLD_DROPIN" "$CHECK"
    rmdir "$DROPIN_DIR" 2>/dev/null
    systemctl --user daemon-reload
    echo "Off. Log out and back in to unload Glance."
    ;;
*)
    echo "Usage: $0 on|off" >&2
    exit 1
    ;;
esac
