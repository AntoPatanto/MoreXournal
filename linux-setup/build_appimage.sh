#!/usr/bin/env bash
#
# Script to package an AppImage. Assumes it is being run from the build
# directory.

set -euo pipefail

ARCH=$(uname -m)

# AppImage root
APPDIR=${APPDIR:-"appimage_staging"}

if [ -z "$APPDIR" ]; then
    echo "Error: APPDIR must be a defined environment variable."
    exit 1
fi

if [ -z "${VERSION:-}" ]; then
    echo "Error: VERSION must be a defined environment variable."
    exit 1
fi

# linuxdeploy and linuxdeploy GTK/Gettext plugin locations
LINUXDEPLOY=${LINUXDEPLOY:-"linuxdeploy.AppImage"}
LINUXDEPLOY_PLUGIN_GTK="linuxdeploy-plugin-gtk.sh"
LINUXDEPLOY_PLUGIN_GETTEXT="linuxdeploy-plugin-gettext.sh"

# Download linuxdeploy and its GTK and Gettext plugins, if they do not yet exist
if [[ ! -f $LINUXDEPLOY_PLUGIN_GTK ]]; then
    wget -c "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"
    chmod +x "$LINUXDEPLOY_PLUGIN_GTK"
fi

if [[ ! -f $LINUXDEPLOY_PLUGIN_GETTEXT ]]; then
    wget -c "https://raw.githubusercontent.com/linuxdeploy/misc-plugins/master/gettext/linuxdeploy-plugin-gettext.sh"
    chmod +x "$LINUXDEPLOY_PLUGIN_GETTEXT"
fi

if [[ ! -f $LINUXDEPLOY ]]; then
    wget -c "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" -O "$LINUXDEPLOY"
    chmod +x "$LINUXDEPLOY"
fi

# Extract tar contents to APPDIR; tar file already contains a top
# level dir, so remove it.
TAR_NAME=$(find packages -maxdepth 1 -name 'morexournal-*.tar.gz' -print -quit)
if [[ -z "$TAR_NAME" ]]; then
    echo "Error: no packages/morexournal-*.tar.gz archive found in $(pwd)"
    exit 1
fi

if [[ ! -d "$APPDIR" ]]; then
    tar xf "$TAR_NAME" --one-top-level="$APPDIR"/usr --strip-components=1
fi

ICON_FILE="$APPDIR"/usr/share/icons/hicolor/scalable/apps/io.github.antopatanto.morexournal.svg
DESKTOP_FILE="$APPDIR"/usr/share/applications/io.github.antopatanto.morexournal.desktop
echo "Use the icon file $ICON_FILE and the desktop file $DESKTOP_FILE"

filename_pattern="morexournal-*$ARCH.AppImage.zsync"
# See https://github.com/AppImage/AppImageSpec/blob/master/draft.md#update-information

if [[ $VERSION = *dev ]]; then
    gh_tag='nightly'  # latest development version
else
    gh_tag='latest'   # latest stable version
fi

export UPD_INFO="gh-releases-zsync|AntoPatanto|MoreXournal|$gh_tag|$filename_pattern"
export VERBOSE=1

# Call through linuxdeploy
./"$LINUXDEPLOY" --appimage-extract-and-run --appdir="$APPDIR" --plugin gtk --plugin gettext --output appimage --icon-file="$ICON_FILE" --desktop-file="$DESKTOP_FILE"
