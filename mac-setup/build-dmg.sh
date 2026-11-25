#!/usr/bin/env bash

set -euo pipefail

[ $# -lt 2 ] && echo "Usage: $0 <.app folder> <output path>" && exit 1

APP_NAME="$1"
OUT_NAME="$2"

TMP_DIR=$(mktemp -d)
TMP_DMG="$TMP_DIR/tmp.dmg"
TMP_MOUNTPOINT="$TMP_DIR/mnt"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "Creating temporary dmg: $TMP_DMG"
echo "Mount point at: $TMP_MOUNTPOINT"
mkdir -p "$TMP_MOUNTPOINT"

# Size the temporary HFS+ image from the .app bundle with headroom so RelWithDebInfo
# bundles do not run out of space (minimum 256 MB).
APP_SIZE_MB=$(du -sm "$APP_NAME" | awk '{print $1}')
DMG_SIZE_MB=$(( APP_SIZE_MB + 64 ))
if (( DMG_SIZE_MB < 256 )); then
    DMG_SIZE_MB=256
fi
echo "App size: ${APP_SIZE_MB}m, creating temporary dmg of size ${DMG_SIZE_MB}m"

hdiutil create -size "${DMG_SIZE_MB}m" -fs HFS+ -volname "MoreXournal" "$TMP_DMG"
hdiutil attach "$TMP_DMG" -mountpoint "$TMP_MOUNTPOINT"
echo "Copying app"
cp -a "$APP_NAME" "$TMP_MOUNTPOINT"/
ln -s /Applications/ "$TMP_MOUNTPOINT"/Applications

# Some race condition makes the CI sometimes fail on this step with "resource busy"
# This fix is from https://github.com/create-dmg/create-dmg/pull/139
unmounting_attempts=0
until
    echo "Unmounting disk image..."
    (( ++unmounting_attempts ))
    exit_code=0
    hdiutil detach "$TMP_MOUNTPOINT" || exit_code=$?
    (( exit_code == 0 )) && break               # nothing goes wrong
    (( exit_code != 16 )) && exit "$exit_code"  # exit with the original exit code
    false
do
    (( unmounting_attempts == 3 )) && exit 16  # patience exhausted, exit with code EBUSY
    echo "Wait a moment..."
    sleep $(( 1 * (2 ** unmounting_attempts) ))
done
unset unmounting_attempts

# hdiutil convert refuses to run if the file already exists, so delete it
rm -f "$OUT_NAME"
hdiutil convert "$TMP_DMG" -format UDZO -o "$OUT_NAME"
