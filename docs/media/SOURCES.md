# Where the media come from

Every still and GIF in this directory was drawn by `replay-player` itself, headless on software GL (Mesa llvmpipe), by
`tools/make_media.sh` (`--frames` then `tools/make_gif.py`), or, for the `browser-*` images, by the browser build in
headless Chromium (`web/smoke/capture.cjs`). Nothing was edited afterwards.

| Media | Replay | Made with | Opened as |
|---|---|---|---|
| `microbiome.png`, `.gif` | microbiome seed 1, 2,000 ticks, a sample every 10 ticks, 32 x 32 field; last state hash `24eaa29109c22277` | [microbiome](https://github.com/Preponderous-Software/microbiome) at `d2fca5f99f331fadaca3856b29b9212317d728f8`: `mb_strata --headless --ticks 2000 --no-hud --window-width 64 --window-height 64 --record microbiome-seed1.usda --record-every 10 --record-field 32` | `.usda`, natively |
| `steading.png`, `.gif` | steading seed 42, 1,000 ticks, a sample every 4; last state hash `011ed2b2f364b21b` | [steading](https://github.com/Preponderous-Software/steading) at `3067c47c06221e44cbc8defdc12d959ae75b8eed`: `steading-replay --new 42 --ticks 1000 --every 4 --version 0.1.0+3067c47c0622 --out steading-seed42.usda` | `.usda`, natively |
| `beyond-nations.png`, `.gif` | Beyond Nations seed 12, 600,000 ticks, 300 samples | the bundle published at `https://danielstephenson.dev/3d/beyond-nations/` (from [Beyond Nations](https://github.com/Preponderous-Software/beyond-nations)' `tools/BeyondNations.HistoryExport` and `tools/history-to-replay.py --stride 2000`, converted by usd-web 0.2.0) | usd-web bundle |
| `kindling.png`, `.gif` | Kindling's restoration, 46 keyframes over 11,700 ticks; last state hash `0b008efbdb48bfa0` | [Kindling](https://github.com/Preponderous-Software/kindling) at `63e9bd4a7eca7f23d113d56fc28b5150d126c9ab`: `kindling --headless --autopilot --replay restoration.usda` | `.usda`, natively |
| `browser-steading.png` | steading seed 42 at tick 600 | the bundle published at `https://danielstephenson.dev/3d/steading/` | browser, 1280 x 720 |
| `browser-phone-microbiome.png` | microbiome seed 1 at tick 1,000 | the bundle published at `https://danielstephenson.dev/3d/microbiome/` (the same run as above: tick 1,000 shows state `8b6446096e0c2f85` in both) | browser, a 390 px phone at 2x |

To make them again (with a player built with `-DREPLAY_PLAYER_WITH_USD=ON`):

```sh
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe tools/make_media.sh build/replay-player \
  microbiome-seed1.usda steading-seed42.usda path/to/beyond-nations-bundle restoration.usda
```
