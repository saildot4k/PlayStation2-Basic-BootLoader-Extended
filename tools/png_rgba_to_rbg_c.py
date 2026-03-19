#!/usr/bin/env python3
import argparse
import pathlib
import struct
import sys
import zlib
from collections import Counter

PNG_SIG = b"\x89PNG\r\n\x1a\n"
LGB1_MAGIC = b"LGB1"


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png_rgba(path: pathlib.Path):
    data = path.read_bytes()
    if data[:8] != PNG_SIG:
        raise ValueError(f"{path}: invalid PNG signature")

    pos = 8
    width = None
    height = None
    bit_depth = None
    color_type = None
    interlace = None
    plte = None
    trns = None
    idat = bytearray()

    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        cdata_start = pos + 8
        cdata_end = cdata_start + length
        cdata = data[cdata_start:cdata_end]
        pos = cdata_end + 4  # skip CRC

        if ctype == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", cdata)
            if comp != 0 or filt != 0:
                raise ValueError(f"{path}: unsupported PNG compression/filter method")
        elif ctype == b"PLTE":
            plte = bytes(cdata)
        elif ctype == b"tRNS":
            trns = bytes(cdata)
        elif ctype == b"IDAT":
            idat.extend(cdata)
        elif ctype == b"IEND":
            break

    if width is None or height is None:
        raise ValueError(f"{path}: missing IHDR")
    if bit_depth != 8:
        raise ValueError(f"{path}: only 8-bit PNG is supported")
    if color_type not in (2, 3, 6):
        raise ValueError(f"{path}: only color types 2 (RGB), 3 (indexed), or 6 (RGBA) are supported")
    if interlace != 0:
        raise ValueError(f"{path}: interlaced PNG is not supported")

    channels = {2: 3, 3: 1, 6: 4}[color_type]
    stride = width * channels
    expected = (stride + 1) * height
    raw = zlib.decompress(bytes(idat))
    if len(raw) != expected:
        raise ValueError(f"{path}: decoded data length mismatch ({len(raw)} != {expected})")

    palette = []
    palette_alpha = []
    if color_type == 3:
        if plte is None or len(plte) % 3 != 0:
            raise ValueError(f"{path}: indexed PNG missing valid PLTE chunk")
        palette_count = len(plte) // 3
        for i in range(palette_count):
            palette.append((plte[i * 3 + 0], plte[i * 3 + 1], plte[i * 3 + 2]))
        palette_alpha = [255] * palette_count
        if trns is not None:
            for i in range(min(len(trns), palette_count)):
                palette_alpha[i] = trns[i]

    rgba = bytearray(width * height * 4)
    prev = bytearray(stride)

    for y in range(height):
        row_start = y * (stride + 1)
        filt = raw[row_start]
        src = raw[row_start + 1:row_start + 1 + stride]
        cur = bytearray(stride)

        for x in range(stride):
            left = cur[x - channels] if x >= channels else 0
            up = prev[x]
            up_left = prev[x - channels] if x >= channels else 0
            val = src[x]

            if filt == 0:
                cur[x] = val
            elif filt == 1:
                cur[x] = (val + left) & 0xFF
            elif filt == 2:
                cur[x] = (val + up) & 0xFF
            elif filt == 3:
                cur[x] = (val + ((left + up) >> 1)) & 0xFF
            elif filt == 4:
                cur[x] = (val + paeth(left, up, up_left)) & 0xFF
            else:
                raise ValueError(f"{path}: unsupported filter type {filt}")

        dst_off = y * width * 4
        if color_type == 2:
            for px in range(width):
                s = px * 3
                d = dst_off + px * 4
                rgba[d + 0] = cur[s + 0]
                rgba[d + 1] = cur[s + 1]
                rgba[d + 2] = cur[s + 2]
                rgba[d + 3] = 255
        elif color_type == 6:
            for px in range(width):
                s = px * 4
                d = dst_off + px * 4
                rgba[d + 0] = cur[s + 0]
                rgba[d + 1] = cur[s + 1]
                rgba[d + 2] = cur[s + 2]
                rgba[d + 3] = cur[s + 3]
        else:  # color type 3
            for px in range(width):
                idx = cur[px]
                if idx >= len(palette):
                    raise ValueError(f"{path}: palette index {idx} out of range")
                d = dst_off + px * 4
                rgba[d + 0] = palette[idx][0]
                rgba[d + 1] = palette[idx][1]
                rgba[d + 2] = palette[idx][2]
                rgba[d + 3] = palette_alpha[idx]

        prev = cur

    return width, height, bytes(rgba)


def rgba_to_rbga(rgba: bytes) -> bytes:
    if len(rgba) % 4 != 0:
        raise ValueError("RGBA buffer length must be divisible by 4")

    out = bytearray(len(rgba))
    for i in range(0, len(rgba), 4):
        dst = i
        r = rgba[i + 0]
        g = rgba[i + 1]
        b = rgba[i + 2]
        a = rgba[i + 3]
        out[dst + 0] = r
        out[dst + 1] = b
        out[dst + 2] = g
        out[dst + 3] = a
    return bytes(out)


def emit_c_array(name: str, blob: bytes) -> str:
    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(blob), 12):
        chunk = blob[i:i + 12]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


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


def pack_lgb1_blob(width: int, height: int, palette_rgba, indices: bytes) -> bytes:
    if len(palette_rgba) < 1 or len(palette_rgba) > 256:
        raise ValueError("palette size must be in range 1..256")
    if len(indices) != width * height:
        raise ValueError("index buffer size mismatch")

    palette_count_field = len(palette_rgba) & 0xFF
    header = bytearray()
    header.extend(LGB1_MAGIC)
    header.extend(struct.pack("<H", width))
    header.extend(struct.pack("<H", height))
    header.append(palette_count_field)
    header.append(0)
    header.extend(b"\x00\x00")

    palette_rbga = bytearray(len(palette_rgba) * 4)
    for i, (r, g, b, a) in enumerate(palette_rgba):
        off = i * 4
        palette_rbga[off + 0] = r
        palette_rbga[off + 1] = b
        palette_rbga[off + 2] = g
        palette_rbga[off + 3] = a

    return bytes(header) + bytes(palette_rbga) + indices


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert PNG files to indexed LGB1 C arrays")
    parser.add_argument("--output", required=True)
    parser.add_argument("--max-colors", type=int, default=255)
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()

    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    entries = []
    for src in args.inputs:
        p = pathlib.Path(src)
        stem = p.stem.lower().replace("-", "_")
        symbol = f"splash_{stem}"
        width, height, rgba = decode_png_rgba(p)
        rgba_pixels = unpack_pixels_rgba(rgba)
        palette_rgba, indices = build_palette_and_indices(rgba_pixels, args.max_colors)
        lgb1 = pack_lgb1_blob(width, height, palette_rgba, indices)
        raw_rbga_size = len(rgba_to_rbga(rgba))
        entries.append((symbol, width, height, lgb1, len(palette_rgba), raw_rbga_size, p))

    with out_path.open("w", encoding="ascii") as f:
        f.write("/* Auto-generated by tools/png_rgba_to_rbg_c.py */\n")
        f.write("/* Embedded splash assets stored as indexed LGB1 blobs. */\n\n")
        for symbol, width, height, blob, palette_size, raw_rbga_size, src_path in entries:
            f.write(f"/* Source: {src_path.as_posix()} */\n")
            f.write(f"/* Palette entries: {palette_size}, raw RBGA bytes: {raw_rbga_size} */\n")
            f.write(f"const unsigned int {symbol}_width = {width}u;\n")
            f.write(f"const unsigned int {symbol}_height = {height}u;\n")
            f.write(f"const unsigned int {symbol}_lgb1_size = {len(blob)}u;\n")
            f.write(emit_c_array(f"{symbol}_lgb1", blob))
            f.write("\n\n")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        raise
