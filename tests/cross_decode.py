"""The player's C++ UWR1 decoder and usd-web's Python decoder agree on every chunk of a bundle.

    python tests/cross_decode.py --decode build/replay-decode BUNDLE_DIR [BUNDLE_DIR ...]

For each bundle, ``replay-decode`` prints each decoded chunk as canonical JSON, and this script
prints usd-web's ``decode_chunk`` result the same way (keys sorted, positions as min + q * step and
widths as w / 255 * widthMax in float64 with "%.17g", ids and colour bytes as integers). The two
must be identical, line for line. Needs usd-web (``pip install usd-web``).
"""

from __future__ import annotations

import argparse
import gzip
import json
import struct
import subprocess
import sys
from pathlib import Path

from usd_web.replay import decode_chunk


def _num(v: float) -> str:
    return f"{v:.17g}"


def _ints(a) -> str:
    return "[" + ",".join(str(int(x)) for x in a) + "]"


def canonical_chunks(bundle: Path) -> list[str]:
    doc = json.loads((bundle / "replay.json").read_text(encoding="utf-8"))
    lines = []
    for c in doc["chunks"]:
        raw = (bundle / c["file"]).read_bytes()
        if raw[:2] == b"\x1f\x8b":
            raw = gzip.decompress(raw)
        count, first = struct.unpack_from("<II", raw, 4)
        samples = []
        for s in decode_chunk(raw, doc):
            fields = ",".join(
                "null" if f is None else _ints(f.ravel()) for f in s["fields"]
            )
            sets = []
            for d, e in zip(doc["sets"], s["sets"], strict=True):
                parts = []
                if d["vertexColor"]:
                    parts.append('"colors":' + _ints(e["colors"].ravel()))
                parts.append('"ids":' + _ints(e["ids"]))
                parts.append(
                    '"points":['
                    + ",".join(_num(float(v)) for v in e["points"].ravel())
                    + "]"
                )
                if d["vertexWidth"]:
                    parts.append(
                        '"widths":['
                        + ",".join(_num(float(v)) for v in e["widths"])
                        + "]"
                    )
                sets.append("{" + ",".join(parts) + "}")
            samples.append(
                '{"fields":[' + fields + '],"sets":[' + ",".join(sets) + "]}"
            )
        lines.append(
            f'{{"count":{count},"first":{first},"samples":[' + ",".join(samples) + "]}"
        )
    return lines


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--decode", required=True, help="path to the replay-decode executable"
    )
    ap.add_argument("bundles", nargs="+", type=Path)
    args = ap.parse_args(argv)
    failed = 0
    for bundle in args.bundles:
        p = subprocess.run(
            [args.decode, str(bundle)], capture_output=True, text=True, check=False
        )
        if p.returncode != 0:
            print(f"{bundle}: replay-decode failed: {p.stderr.strip()}")
            failed += 1
            continue
        cpp = p.stdout.splitlines()
        py = canonical_chunks(bundle)
        if cpp == py:
            print(f"{bundle}: {len(py)} chunk(s) identical")
            continue
        failed += 1
        for i, (a, b) in enumerate(zip(cpp, py, strict=False)):
            if a != b:
                k = (
                    next(j for j in range(min(len(a), len(b))) if a[j] != b[j])
                    if a[: len(b)] != b[: len(a)]
                    else min(len(a), len(b))
                )
                print(
                    f"{bundle}: chunk {i} differs at byte {k}:\n  C++:    {a[max(0, k - 80) : k + 80]}\n  Python: {b[max(0, k - 80) : k + 80]}"
                )
                break
        else:
            print(f"{bundle}: {len(cpp)} chunk(s) from C++, {len(py)} from Python")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
