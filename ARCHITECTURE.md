# Architecture

```text
            desktop                                     browser
  run.usdc / run.usda      replay.json + r.*.uwr        replay.json + r.*.uwr (fetched, gunzipped
          |                (read from disk, gunzip.cpp)  by the page with DecompressionStream)
  usd_source.cpp                  |                            |
  (usd-recorder's C++             +------------+---------------+
   reader, OpenUSD)                            |
          |                              bundle.cpp (UWR1 decoder)
          +-------------> ReplayModel <--------+          src/model
                              |
                          Playback                         src/play
                              |
                         StrataScene                       src/render  (Strata C ABI)
                              |
               +--------------+--------------+
         NativeUi (Strata rects,       web/index.html (DOM controls,
         text, keys, mouse)            pointer and pinch -> rp_* exports)
         src/ui, app/main.cpp          app/web_main.cpp
```

| Path | What |
|---|---|
| `src/model/json.*` | a small JSON reader for `replay.json`, with a depth limit |
| `src/model/gunzip.*` | RFC 1951/1952 inflate with a CRC and an output cap, for bundles read from disk |
| `src/model/model.*` | `ReplayModel`: definitions, samples, field frames, held stats, bounds |
| `src/model/bundle.*` | `replay.json` and UWR1: parse, decode, apply, and the canonical dump for cross-checks |
| `src/model/usd_source.*` | a `usd_recorder::Replay` (OpenUSD) to a `ReplayModel` (`REPLAY_PLAYER_WITH_USD` only) |
| `src/play/playback.*` | the clock, seeking, stepping, interpolation |
| `src/render/scene.*` | `StrataScene` and the orbit camera |
| `src/ui/hud.*` | the desktop HUD and input |
| `app/main.cpp`, `app/web_main.cpp` | the desktop program; the browser's exported C surface |
| `tools/` | `replay-decode`, `replay-compare`, `make_media.sh`, `make_gif.py` |

## The model

`ReplayModel` mirrors usd-recorder's Python `Replay`: metadata, playback rate, sample ticks and state hashes, entity sets
(per-sample ids and points, constant or per-entity colours and widths), fields (a `cols x rows` grid whose colours are
sampled at some sample ticks), stats and static scene points. Two things come from the bundle: samples may be thinned
(`keptEvery`), and a bundle's chunks arrive over time, so `loaded` records which samples are present.

- **Ids** are dense per set (0..n-1 in first-seen order), as usd-web's encoder numbers them. The USD path renumbers the
  same way, so a native model and its bundle have identical ids.
- **Fields** are kept as frames (`FieldFrame`: the sample it was sampled at, and RGBA8 texels). `fieldAt(field, k)`
  returns the latest frame at or before sample `k`: the hold REPLAY.md requires. A bundle restates every field at the
  start of each chunk, so a loaded sample always finds a frame.
- **Stats** are stored already held, one value per sample (a bundle carries them that way).

## Colour

Strata writes colours to the framebuffer as they are given, with no sRGB encoding at the end (its shaders write
`fragColor` directly and there is no sRGB framebuffer). So the player hands Strata **display (sRGB) values**:

- a bundle's colours are sRGB bytes already, divided by 255;
- a `.usd` replay's colours are linear (REPLAY.md § Colours) and are encoded to sRGB once, when the replay is loaded.

Field textures are therefore uploaded exactly as the bundle stores them, and an unlit field shows exactly the colours
usd-web's player shows. RFC 0025 §6 expected to convert the bundle's sRGB bytes to linear 8-bit texels and worried
about banding in dark colours; that conversion is not needed, so no sRGB texture flag is asked of Strata. Lit entities
are shaded in that same display space.

## Coordinates and drawing

Replays are Z-up; Strata is +Y up ("+X east, +Y up, +Z south"). A replay point `(x, y, z)` is drawn at `(x, z, -y)`, as
usd-web maps it to three.js.

| Replay part | Strata |
|---|---|
| entity set | `strata_submit_primitives`, one call a frame for all sets, `STRATA_PRIMITIVE_SPHERE` or `_CUBE`, scale = width |
| field | one unlit textured quad (`strata_add_textured_mesh`, `STRATA_MESH_UNLIT`) with a `cols x rows` texture, `STRATA_TEXTURE_NEAREST` (ABI 1.10) and clamped; `strata_update_texture_rgba8` when the held frame changes |
| scene points | primitives, every frame |
| lighting | `STRATA_LIGHTING_SUN`, no fog, the sun fixed at usd-web's light direction (0.4, 1, 0.6); two shadow cascades unless `--no-shadows` |
| camera | an orbit with usd-web's framing: the bounds' diagonal fits the narrower field of view, `theta` 0.6 and `phi` 0.75, or the bundle's `view` |
| HUD (desktop) | `strata_submit_rects` (ABI 1.11) panels and `strata_submit_text` (8 x 8 font, 16 px from 600 px tall, 8 px below) |

A field larger than `STRATA_MAX_TEXTURE_SIZE` on a side is not drawn (no published replay comes near it).

## Playback

`Playback` is usd-web's `viewer/usd-web-replay.js` in C++: the head is a fractional sample index; each frame the head's
tick advances by `dt x playbackRate x speed` (with `dt` clamped to 0.25 s) and maps back to a head; at the end it loops
(or stops with `L` off); stepping pauses; seeking is by tick or sample. `interpolatedPoints` moves each id of the current
sample towards the next sample's position for the same id, and leaves an id that is not in the next sample where it was.
An unloaded sample makes playback wait rather than skip.

## The browser

`app/web_main.cpp` exports a small C surface; `web/index.html` owns fetching, gunzip (`DecompressionStream`), the DOM
controls and input:

| Export | |
|---|---|
| `rp_open(json, len)` | parse `replay.json`, build the scene; 0 on success, else `rp_last_error()` |
| `rp_add_chunk(index, bytes, len)` | decode and store one chunk (gunzipped or gzip'd) |
| `rp_play`, `rp_pause`, `rp_seek(tick)`, `rp_seek_sample(k)`, `rp_step(d)`, `rp_set_speed(s)` | playback |
| `rp_set_interpolate(on)`, `rp_set_shadows(on)` | options |
| `rp_orbit(dx, dy)`, `rp_pan(dx, dy)`, `rp_zoom(f)`, `rp_reset_view()`, `rp_resize(w, h, reframe)` | camera and canvas |
| `rp_frame(dt, force)` | advance and draw when something changed; called from `requestAnimationFrame` |
| `rp_state()` | JSON: playing, waiting, speed, sample, head, tick, hash, loaded, frames, stats |

Strata draws into a WebGL 2 context the program creates on `#view` (`STRATA_SURFACE_HOST_CONTEXT`), not into an SDL
window, so SDL never takes the page's keyboard or pointer events. The controls are DOM elements because Strata's events
have no touch input and its text is an 8 x 8 bitmap font. The page keeps usd-web's markup, classes, CSS and the
`window.__usdWebReplay` hook (`ready, samples, chunks, loaded, tick, sample, hash, playing, frames, errors, speed`, and
`play(), pause(), seek(tick), render()`), so a test written for one drives the other. The build uses no threads, so no
COOP/COEP headers are needed.

Measured on this build: `replay-player.wasm` 1,043,378 bytes, 338,066 gzip'd (`-9`); `replay-player.js` 45,398
gzip'd. In headless Chromium on SwiftShader (a CPU rasteriser, so a floor rather than a measurement of a GPU), the
first frame of a small bundle came about 0.5 s after navigation.

## Untrusted input

Replays and bundles are files from other people.

- `replay.json`: a depth limit (64), every size checked (samples, ticks and hashes agree; at most 4,096 sets and 256
  fields; a field at most 2^24 cells; chunks cover the samples exactly and in order; chunk file names are plain names,
  never paths).
- UWR1: every count is checked against the bytes that remain *before* anything is allocated; an id past the set's id
  count, trailing bytes, or a chunk whose first sample or count disagrees with the index is refused.
- gunzip: an output cap (1 GiB), a CRC-32 and length check.
- `.usd`: usd-recorder's reader caps array memory and checks every length it indexes.
- The page only fetches relative `?data=` paths on its own origin.

## Decisions (RFC 0025's open questions, taken at the RFC's recommended defaults)

- **D1, the browser input:** usd-web bundles (option C). Raw `.usdc` in the browser (OpenUSD in wasm) is deferred.
- **D2, placement:** the C++ reader is in usd-recorder (`cpp/reader`, an optional target); the decoder and the player
  are here, in a new public MIT repository named `replay-player`.
- **D3, the native reader's dependency:** OpenUSD v26.08, the build Strata pins, from Strata's prebuilt installs.
- **D4, Strata ask S1 (nearest-filter textures):** already met. Strata v0.5.0 shipped `STRATA_TEXTURE_NEAREST` (ABI
  1.10), so the CPU-upscaling workaround of RFC §6 was never written. The player requires ABI 1.10 or later.
- **D5, budgets:** 600 KB for the gzip'd `.wasm` is enforced in CI (338 KB today). The frame-rate budgets are not
  measured yet: CI has no GPU and no phone.
- **Open question 2, replace or coexist:** coexist. usd-web's three.js player stays the browser player for the
  portfolio and producers; this page passes usd-web's smoke checks and fits the size budget, but the R3 gate also asks
  for frame rates measured on a desktop GPU and a real phone, and the decision is the owner's.
- **Open question 7, look:** the fixed sun with shadows on the desktop; in the browser shadows are off on touch screens
  (`?shadows=1` turns them on) and on elsewhere.
- **Open question 6, hosting:** not decided here; nothing is deployed by this repository's workflows.
