#!/bin/sh
# Sets KWrite's editor font, which clips (text dropped on the desktop, see
# main.cpp) are shown in. KWrite's default is small, and clips are often
# shown shrunk at the sides of the screen.
#
#   setup-kwrite.sh            Noto Sans Mono, 16 pt
#   setup-kwrite.sh 20         another size
#   setup-kwrite.sh 16 "Noto Sans"   another font
#
# This is KWrite's own setting (~/.config/kwriterc), so it applies to every
# KWrite window; it can also be changed in KWrite: Settings > Configure
# KWrite > Appearance > Fonts. Takes effect for KWrite windows opened after.
set -e
size=${1:-16}
family=${2:-Noto Sans Mono}
kwriteconfig6 --file kwriterc --group "KTextEditor Renderer" --key "Text Font" \
    "$family,$size,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"
echo "KWrite font: $family, $size pt"
