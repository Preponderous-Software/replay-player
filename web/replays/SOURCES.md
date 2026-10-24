# The replays the browser site offers

`index.json` lists the replays in the page's picker; the page opens the first when no `?data=` is given.
`tools/assemble_site.sh` copies this directory to the site and adds Kindling's restoration from the test corpus. Each
bundle is a usd-web bundle (`replay.json` + gzip'd UWR1 chunks), copied unchanged from where it was already published;
the replays are the runs behind `docs/media` (see [docs/media/SOURCES.md](../../docs/media/SOURCES.md)).

| Directory | Replay | Copied from | Producer |
|---|---|---|---|
| `steading/` | steading seed 42, 1,000 ticks, 251 samples; last state hash `011ed2b2f364b21b` | `https://danielstephenson.dev/3d/steading/` | [steading](https://github.com/Preponderous-Software/steading) (`steading-replay`), MIT |
| `microbiome/` | microbiome seed 1, 2,000 ticks, 201 samples; last state hash `24eaa29109c22277` | `https://danielstephenson.dev/3d/microbiome/` | [microbiome](https://github.com/Preponderous-Software/microbiome) (`mb_strata --record`), Preponderous-NC |
| `beyond-nations/` | Beyond Nations seed 12, 600,000 ticks, 300 samples | `https://danielstephenson.dev/3d/beyond-nations/` | [Beyond Nations](https://github.com/Preponderous-Software/beyond-nations) (`tools/history-to-replay.py`), Preponderous-NC |
| `kindling/` (at assembly) | Kindling's restoration, 46 keyframes over 11,700 ticks | `tests/corpus/kindling-restoration/` | [Kindling](https://github.com/Preponderous-Software/kindling), MIT |

Each `replay.json` carries its producer's version and command in `meta`. The replays are data from those projects and
stay under their licences. Only the replay picked is fetched, chunk by chunk (the largest, microbiome, is 1.3 MB).
