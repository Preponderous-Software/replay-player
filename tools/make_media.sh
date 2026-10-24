#!/usr/bin/env bash
# Renders the README's stills and GIFs (docs/media/) headless with replay-player.
#
#   tools/make_media.sh PLAYER MICROBIOME.usda STEADING.usda BEYOND_NATIONS_BUNDLE KINDLING.usda [OUT_DIR]
#
# PLAYER is a replay-player built with -DREPLAY_PLAYER_WITH_USD=ON (the .usda files are opened
# natively; the Beyond Nations replay is a usd-web bundle). docs/media/SOURCES.md says where each
# replay came from. Needs Python with Pillow for the GIFs. Software GL is fine:
#   LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe tools/make_media.sh ...
set -euo pipefail
player=$1 mb=$2 st=$3 bn=$4 kd=$5 out=${6:-docs/media}
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$out"

still() {  # name source tick extra-args...
  local name=$1 src=$2 at=$3
  shift 3
  "$player" "$src" --headless --width 1280 --height 720 --at "$at" --screenshot "$out/$name.png" "$@"
}
gif() {  # name source from-tick speed frames extra-args...
  local name=$1 src=$2 at=$3 speed=$4 count=$5
  shift 5
  mkdir -p "$tmp/$name"
  "$player" "$src" --headless --width 1280 --height 720 --at "$at" --speed "$speed" --frames "$tmp/$name" \
    --count "$count" --fps 12 "$@"
  python3 "$here/make_gif.py" "$tmp/$name" "$out/$name.gif" --fps 12 --width 720 --colors 192
}

still microbiome "$mb" 1000
gif microbiome "$mb" 0 4 72 --spin 8
still steading "$st" 600
gif steading "$st" 0 3 72 --spin 8
still beyond-nations "$bn" 360000 --zoom 0.55
gif beyond-nations "$bn" 240000 1 72 --zoom 0.55 --spin 6
still kindling "$kd" 11700 --zoom 0.8
gif kindling "$kd" 0 5 96 --zoom 0.8 --spin 10
