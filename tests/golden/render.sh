#!/usr/bin/env bash
# Renders the golden-image set headless: each corpus replay at fixed ticks, 640 x 360, HUD on.
#
#   tests/golden/render.sh PLAYER OUT_DIR [bundle|usd]
#
# `bundle` (the default) opens the usd-web bundles in tests/; `usd` opens the .usdc files with a
# player built with -DREPLAY_PLAYER_WITH_USD=ON. Both must match the same goldens
# (tests/golden/*.png, compared with Strata's strata-imgcmp in CI): the bundle is the .usdc
# quantized, which does not move anything by a visible amount.
set -euo pipefail
player=$1 out=$2 kind=${3:-bundle}
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$out"
render() {  # name bundle usdc tick
  local src=$root/$2
  [ "$kind" = usd ] && src=$root/$3
  "$player" "$src" --headless --width 640 --height 360 --at "$4" --screenshot "$out/$1.png" 2> "$out/$1.log" ||
    { cat "$out/$1.log"; exit 1; }
}
render scenario-30 tests/data/scenario tests/data/scenario.usda 30
render steading-120 tests/corpus/steading-seed42 tests/corpus/steading-seed42.usdc 120
render kindling-5000 tests/corpus/kindling-restoration tests/corpus/kindling-restoration.usdc 5000
render kindling-11700 tests/corpus/kindling-restoration tests/corpus/kindling-restoration.usdc 11700
