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

# Pinned release version / commit SHAs and SHA-256 checksums.
# Upstream linuxdeploy-plugin-gtk and misc-plugins do not publish tagged releases,
# so their scripts are pinned to immutable commit SHAs.
LINUXDEPLOY_VERSION="1-alpha-20251107-1"
LINUXDEPLOY_PLUGIN_GTK_COMMIT="3b67a1d1c1b0c8268f57f2bce40fe2d33d409cea"
LINUXDEPLOY_PLUGIN_GTK_SHA256="b0f4cbc684a0103a9651f0955b635eaea0096b3a66c0f5a2c2aa337960375171"
LINUXDEPLOY_PLUGIN_GETTEXT_COMMIT="e757980469bc9f3857152376cc4126caee65fe7a"
LINUXDEPLOY_PLUGIN_GETTEXT_SHA256="ba47c4bc2a41ff88d22c453b0571740b57b212aa1d69da61728b6e84d661243d"

case "$ARCH" in
    x86_64)  LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d" ;;
    aarch64) LINUXDEPLOY_SHA256="620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff" ;;
    armhf)   LINUXDEPLOY_SHA256="e359161979fa4bee50b92ce7102fb510299caebf34f711d983fba7a8f4bb1c2e" ;;
    i386)    LINUXDEPLOY_SHA256="a2a88d142aac42db779483ca07c10dbf318b27f514691107fc88a202faae17b5" ;;
    *)       LINUXDEPLOY_SHA256="" ;;
esac

download_and_verify() {
    local url="$1"
    local dest="$2"
    local expected_sha256="$3"
    local tmp="${dest}.tmp"

    rm -f "$tmp"
    wget -nv -O "$tmp" "$url"
    if ! printf '%s  %s\n' "$expected_sha256" "$tmp" | sha256sum -c -; then
        rm -f "$tmp"
        echo "Error: SHA-256 verification failed for $url" >&2
        exit 1
    fi
    chmod +x "$tmp"
    mv "$tmp" "$dest"
}

# Download linuxdeploy and its GTK and Gettext plugins, if they do not yet exist
if [[ ! -f $LINUXDEPLOY_PLUGIN_GTK ]]; then
    download_and_verify \
        "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/${LINUXDEPLOY_PLUGIN_GTK_COMMIT}/linuxdeploy-plugin-gtk.sh" \
        "$LINUXDEPLOY_PLUGIN_GTK" \
        "$LINUXDEPLOY_PLUGIN_GTK_SHA256"
fi

if [[ ! -f $LINUXDEPLOY_PLUGIN_GETTEXT ]]; then
    download_and_verify \
        "https://raw.githubusercontent.com/linuxdeploy/misc-plugins/${LINUXDEPLOY_PLUGIN_GETTEXT_COMMIT}/gettext/linuxdeploy-plugin-gettext.sh" \
        "$LINUXDEPLOY_PLUGIN_GETTEXT" \
        "$LINUXDEPLOY_PLUGIN_GETTEXT_SHA256"
fi

if [[ ! -f $LINUXDEPLOY ]]; then
    if [[ -z "$LINUXDEPLOY_SHA256" ]]; then
        echo "Error: no pinned linuxdeploy SHA-256 for architecture '$ARCH'." >&2
        exit 1
    fi
    download_and_verify \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/linuxdeploy-${ARCH}.AppImage" \
        "$LINUXDEPLOY" \
        "$LINUXDEPLOY_SHA256"
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
