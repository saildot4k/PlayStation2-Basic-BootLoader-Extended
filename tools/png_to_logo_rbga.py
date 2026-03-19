#!/usr/bin/env python3
"""Convert PNG into compact indexed LOGO.BIN for PS2BBL custom splash override.

Output format (LGB1):
- 12-byte header
- palette entries in RBGA (4 bytes each)
- 8-bit index buffer (width*height bytes)
"""

import argparse
import pathlib
import struct
import sys
from collections import Counter

from png_rgba_to_rbg_c import decode_png_rgba, rgba_to_rbga


MAGIC = b"LGB1"


def unpack_pixels_rgba(rgba: bytes):
    return [tuple(rgba[i:i + 4]) for i in range(0, len(rgba), 4)]


def color_distance_sq(a, b) -> int:
    dr = int(a[0]) - int(b[0])
    dg = int(a[1]) - int(b[1])
    db = int(a[2]) - int(b[2])
    da = int(a[3]) - int(b[3])
    return dr * dr + dg * dg + db * db + (da * da * 2)


def build_palette_and_indices(rgba_pixels, max_colors: int):
    if max_colors < 1 or max_colors > 256:
        raise ValueError("max_colors must be in range 1..256")

    counts = Counter(rgba_pixels)
    sorted_colors = [c for c, _ in counts.most_common()]

    if len(sorted_colors) <= max_colors:
        palette_rgba = sorted_colors
    else:
        palette_rgba = sorted_colors[:max_colors]

    direct_map = {c: i for i, c in enumerate(palette_rgba)}
    nearest_cache = {}
    indices = bytearray(len(rgba_pixels))

    for i, color in enumerate(rgba_pixels):
        direct = direct_map.get(color)
        if direct is not None:
            indices[i] = direct
            continue

        cached = nearest_cache.get(color)
        if cached is None:
            best_idx = 0
            best_dist = color_distance_sq(color, palette_rgba[0])
            for idx in range(1, len(palette_rgba)):
                dist = color_distance_sq(color, palette_rgba[idx])
                if dist < best_dist:
                    best_dist = dist
                    best_idx = idx
            nearest_cache[color] = best_idx
            cached = best_idx

        indices[i] = cached

    return palette_rgba, bytes(indices)


def pack_indexed_logo_bin(width: int, height: int, palette_rgba, indices: bytes) -> bytes:
    if len(palette_rgba) < 1 or len(palette_rgba) > 256:
        raise ValueError("palette size must be in range 1..256")
    if len(indices) != width * height:
        raise ValueError("index buffer size mismatch")

    palette_count_field = len(palette_rgba) & 0xFF
    header = bytearray()
    header.extend(MAGIC)
    header.extend(struct.pack("<H", width))
    header.extend(struct.pack("<H", height))
    header.append(palette_count_field)
    header.append(0)  # flags
    header.extend(b"\x00\x00")  # reserved

    palette_rbga = bytearray(len(palette_rgba) * 4)
    for i, (r, g, b, a) in enumerate(palette_rgba):
        off = i * 4
        palette_rbga[off + 0] = r
        palette_rbga[off + 1] = b
        palette_rbga[off + 2] = g
        palette_rbga[off + 3] = a

    return bytes(header) + bytes(palette_rbga) + indices


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert PNG into compact indexed LOGO.BIN"
    )
    parser.add_argument("input", help="Input PNG path")
    parser.add_argument(
        "-o",
        "--output",
        default="LOGO.BIN",
        help="Output file path (default: LOGO.BIN)",
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
    parser.add_argument(
        "--max-colors",
        type=int,
        default=255,
        help="Maximum palette colors for 8-bit indexed output (default: 255)",
    )
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    width, height, rgba = decode_png_rgba(input_path)
    if width != args.width or height != args.height:
        raise ValueError(
            f"{input_path}: expected {args.width}x{args.height} but got {width}x{height}"
        )

    rgba_pixels = unpack_pixels_rgba(rgba)
    palette_rgba, indices = build_palette_and_indices(rgba_pixels, args.max_colors)
    blob = pack_indexed_logo_bin(width, height, palette_rgba, indices)

    # Legacy size shown for quick comparison in logs.
    raw_rbga_size = len(rgba_to_rbga(rgba))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(blob)

    print(
        f"Wrote {len(blob)} bytes to {output_path} "
        f"({width}x{height}, indexed palette={len(palette_rgba)}, raw_rbga={raw_rbga_size} bytes)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise
