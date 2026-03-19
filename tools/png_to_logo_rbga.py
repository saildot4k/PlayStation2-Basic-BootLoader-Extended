#!/usr/bin/env python3
"""Convert a PNG into a raw RBGA logo blob for PS2BBL custom splash override.

Output format is headerless RBGA bytes (R, B, G, A) with fixed dimensions by default.
"""

import argparse
import pathlib
import sys

from png_rgba_to_rbg_c import decode_png_rgba, rgba_to_rbga


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert PNG into raw RBGA bytes for custom CWD LOGO.BIN"
    )
    parser.add_argument("input", help="Input PNG path")
    parser.add_argument(
        "-o",
        "--output",
        default="LOGO.BIN",
        help="Output RBGA file path (default: LOGO.BIN)",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=256,
        help="Expected width (default: 256)",
    )
    parser.add_argument(
        "--height",
        type=int,
        default=64,
        help="Expected height (default: 64)",
    )
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    width, height, rgba = decode_png_rgba(input_path)
    if width != args.width or height != args.height:
        raise ValueError(
            f"{input_path}: expected {args.width}x{args.height} but got {width}x{height}"
        )

    rbga = rgba_to_rbga(rgba)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(rbga)

    print(
        f"Wrote {len(rbga)} bytes to {output_path} "
        f"({width}x{height} RBGA, headerless)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise
