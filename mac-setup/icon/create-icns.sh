#!/usr/bin/env bash
set -euo pipefail
# https://stackoverflow.com/questions/12306223/how-to-manually-create-icns-files-using-iconutil

script_dir="$(cd "$(dirname "$0")" && pwd)"
svg_icon="$script_dir/../../ui/pixmaps/io.github.antopatanto.morexournal.svg"
out_icns="${1:-$script_dir/morexournal.icns}"

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

src_png="$work_dir/Icon1024.png"
rsvg_bin=""
if [ -n "${GTKDIR:-}" ] && [ -x "$GTKDIR/bin/rsvg-convert" ]; then
  rsvg_bin="$GTKDIR/bin/rsvg-convert"
elif command -v rsvg-convert >/dev/null 2>&1; then
  rsvg_bin="$(command -v rsvg-convert)"
fi

if [ -n "$rsvg_bin" ] && [ -f "$svg_icon" ]; then
  echo "Rendering macOS icon from $svg_icon using $rsvg_bin"
  "$rsvg_bin" -w 1024 -h 1024 "$svg_icon" -o "$src_png"
else
  echo "rsvg-convert not found; falling back to $script_dir/Icon1024.png"
  cp "$script_dir/Icon1024.png" "$src_png"
fi

iconset="$work_dir/MoreXournal.iconset"
mkdir -p "$iconset"
sips -z 16 16     "$src_png" --out "$iconset/icon_16x16.png" >/dev/null
sips -z 32 32     "$src_png" --out "$iconset/icon_16x16@2x.png" >/dev/null
sips -z 32 32     "$src_png" --out "$iconset/icon_32x32.png" >/dev/null
sips -z 64 64     "$src_png" --out "$iconset/icon_32x32@2x.png" >/dev/null
sips -z 128 128   "$src_png" --out "$iconset/icon_128x128.png" >/dev/null
sips -z 256 256   "$src_png" --out "$iconset/icon_128x128@2x.png" >/dev/null
sips -z 256 256   "$src_png" --out "$iconset/icon_256x256.png" >/dev/null
sips -z 512 512   "$src_png" --out "$iconset/icon_256x256@2x.png" >/dev/null
sips -z 512 512   "$src_png" --out "$iconset/icon_512x512.png" >/dev/null
cp "$src_png" "$iconset/icon_512x512@2x.png"
iconutil -c icns "$iconset" -o "$out_icns"
