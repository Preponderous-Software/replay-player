"""Frames (PNG, from ``replay-player --frames``) to an optimized GIF for the README.

    python tools/make_gif.py FRAMES_DIR OUT.gif [--fps 12] [--width 640] [--colors 128]

Needs Pillow. One palette is made from a sample of the frames and used for all of them, so the
colours do not flicker from frame to frame.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("frames", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--fps", type=float, default=12)
    ap.add_argument(
        "--width",
        type=int,
        default=0,
        help="scale to this width (0 keeps the frames' size)",
    )
    ap.add_argument("--colors", type=int, default=128)
    args = ap.parse_args(argv)
    paths = sorted(args.frames.glob("frame_*.png"))
    if not paths:
        print(f"no frame_*.png in {args.frames}", file=sys.stderr)
        return 1
    frames = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if args.width and im.width != args.width:
            im = im.resize(
                (args.width, round(im.height * args.width / im.width)), Image.LANCZOS
            )
        frames.append(im)
    # A shared palette from a strip of evenly spaced frames.
    picks = frames[:: max(1, len(frames) // 8)]
    strip = Image.new("RGB", (frames[0].width, frames[0].height * len(picks)))
    for i, im in enumerate(picks):
        strip.paste(im, (0, i * im.height))
    palette = strip.quantize(colors=args.colors, method=Image.MEDIANCUT)
    out = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in frames]
    out[0].save(
        args.out,
        save_all=True,
        append_images=out[1:],
        duration=round(1000 / args.fps),
        loop=0,
        optimize=True,
        disposal=1,
    )
    print(f"{args.out}: {len(out)} frames, {args.out.stat().st_size:,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
