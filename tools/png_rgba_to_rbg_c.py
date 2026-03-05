#!/usr/bin/env python3
import argparse
import pathlib
import struct
import sys
import zlib

PNG_SIG = b"\x89PNG\r\n\x1a\n"


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


def nearest_color_index(color, palette):
    r, g, b, _ = color
    best_idx = 0
    best_dist = 1 << 30

    for i, p in enumerate(palette):
        pr, pg, pb, _ = p
        dr = r - pr
        dg = g - pg
        db = b - pb
        dist = dr * dr + dg * dg + db * db
        if dist < best_dist:
            best_dist = dist
            best_idx = i
            if dist == 0:
                break

    return best_idx


def quantize_to_t8(rgba: bytes):
    if len(rgba) % 4 != 0:
        raise ValueError("RGBA buffer length must be divisible by 4")

    counts = {}
    for i in range(0, len(rgba), 4):
        c = (rgba[i + 0], rgba[i + 1], rgba[i + 2], rgba[i + 3])
        counts[c] = counts.get(c, 0) + 1

    unique_count = len(counts)
    if unique_count <= 256:
        palette = list(counts.keys())
    else:
        # Keep the 256 most frequent colors and map all others to nearest palette entry.
        palette = [
            item[0]
            for item in sorted(counts.items(), key=lambda kv: kv[1], reverse=True)[:256]
        ]

    mapping = {}
    for idx, color in enumerate(palette):
        mapping[color] = idx

    if unique_count > 256:
        for color in counts:
            if color not in mapping:
                mapping[color] = nearest_color_index(color, palette)

    indices = bytearray(len(rgba) // 4)
    out_pos = 0
    for i in range(0, len(rgba), 4):
        c = (rgba[i + 0], rgba[i + 1], rgba[i + 2], rgba[i + 3])
        indices[out_pos] = mapping[c]
        out_pos += 1

    # Emit full 256-entry CLUT for GS PSMT8 upload.
    clut = bytearray(256 * 4)
    for i in range(min(len(palette), 256)):
        r, g, b, a = palette[i]
        clut[i * 4 + 0] = r
        clut[i * 4 + 1] = b  # project RBG convention (RBGA)
        clut[i * 4 + 2] = g
        clut[i * 4 + 3] = a

    return bytes(indices), bytes(clut), 256


def emit_c_array(name: str, blob: bytes) -> str:
    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(blob), 12):
        chunk = blob[i:i + 12]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert PNG files to paletted T8 + RBGA CLUT C arrays")
    parser.add_argument("--output", required=True)
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
        indices, clut, clut_entries = quantize_to_t8(rgba)
        entries.append((symbol, width, height, indices, clut, clut_entries, p))

    with out_path.open("w", encoding="ascii") as f:
        f.write("/* Auto-generated by tools/png_rgba_to_rbg_c.py */\n")
        f.write("/* Pixel layout: T8 indexed texture + RBGA CLUT entries (R, B, G, A). */\n\n")
        for symbol, width, height, idx_blob, clut_blob, clut_entries, src_path in entries:
            f.write(f"/* Source: {src_path.as_posix()} */\n")
            f.write(f"const unsigned int {symbol}_width = {width}u;\n")
            f.write(f"const unsigned int {symbol}_height = {height}u;\n")
            f.write(f"const unsigned int {symbol}_clut_entries = {clut_entries}u;\n")
            f.write(emit_c_array(f"{symbol}_t8", idx_blob))
            f.write("\n")
            f.write(emit_c_array(f"{symbol}_clut_rbga", clut_blob))
            f.write("\n\n")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        raise
