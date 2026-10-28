#!/usr/bin/env bash
# Assembles the browser site from a web build: the page, its loading shell and ?bench harness, the player, the
# replays it offers in its picker (web/replays, plus Kindling's restoration from the test corpus)
# and the test bundles.
#
#   tools/assemble_site.sh BUILD_WEB_DIR SITE_DIR
set -euo pipefail
build=${1:?usage: assemble_site.sh BUILD_WEB_DIR SITE_DIR}
site=${2:?usage: assemble_site.sh BUILD_WEB_DIR SITE_DIR}
root=$(cd "$(dirname "$0")/.." && pwd)

rm -rf "$site"
mkdir -p "$site/data"
cp "$root/web/index.html" "$root/web/bench.js" "$build/replay-player.js" "$build/replay-player.wasm" "$site/"
# The loading shell (web/strata-kit, vendored from Strata) and its bridge to this build, and the
# .wasm's size on the page: the arcade compresses on the fly and sends no Content-Length.
cp "$root/web/strata-boot.js" "$site/"
mkdir -p "$site/strata-kit"
cp "$root/web/strata-kit/strata-loader.js" "$root/web/strata-kit/LICENSE" "$site/strata-kit/"
bytes=$(wc -c < "$build/replay-player.wasm")
grep -q 'data-bytes="0"' "$site/index.html"
sed -i "s/data-bytes=\"0\"/data-bytes=\"$bytes\"/" "$site/index.html"

# The picker's replays (web/replays/index.json lists them).
cp -r "$root/web/replays" "$site/replays"
cp -r "$root/tests/corpus/kindling-restoration" "$site/replays/kindling"

# The test bundles, for the smoke tests (?data=data/NAME/).
cp -r "$root/tests/data/scenario" "$site/data/scenario"
for d in "$root"/tests/corpus/*/; do cp -r "$d" "$site/data/$(basename "$d")"; done

# Every replay the picker lists must be there.
python3 - "$site" <<'PY'
import json, pathlib, sys
site = pathlib.Path(sys.argv[1])
for r in json.loads((site / "replays/index.json").read_text())["replays"]:
    doc = json.loads((site / r["data"] / "replay.json").read_text())
    for c in doc["chunks"]:
        assert (site / r["data"] / c["file"]).is_file(), f"{r['id']}: {c['file']} is missing"
    print(f"{r['id']}: {doc['samples']} samples in {len(doc['chunks'])} chunk(s)")
PY
du -sh "$site"
