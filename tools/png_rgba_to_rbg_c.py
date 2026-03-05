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
        elif ctype == b"IDAT":
            idat.extend(cdata)
        elif ctype == b"IEND":
            break

    if width is None or height is None:
        raise ValueError(f"{path}: missing IHDR")
    if bit_depth != 8 or color_type != 6:
        raise ValueError(f"{path}: only 8-bit RGBA PNG (color type 6) is supported")
    if interlace != 0:
        raise ValueError(f"{path}: interlaced PNG is not supported")

    raw = zlib.decompress(bytes(idat))
    stride = width * 4
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError(f"{path}: decoded data length mismatch ({len(raw)} != {expected})")

    out = bytearray(width * height * 4)
    prev = bytearray(stride)

    for y in range(height):
        row_start = y * (stride + 1)
        filt = raw[row_start]
        src = raw[row_start + 1:row_start + 1 + stride]
        cur = bytearray(stride)

        for x in range(stride):
            left = cur[x - 4] if x >= 4 else 0
            up = prev[x]
            up_left = prev[x - 4] if x >= 4 else 0
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

        dst_off = y * stride
        out[dst_off:dst_off + stride] = cur

        prev = cur

    return width, height, bytes(out)


def rgba_to_rbga(rgba_blob: bytes) -> bytes:
    out = bytearray(len(rgba_blob))
    for i in range(0, len(rgba_blob), 4):
        r = rgba_blob[i + 0]
        g = rgba_blob[i + 1]
        b = rgba_blob[i + 2]
        a = rgba_blob[i + 3]
        out[i + 0] = r
        out[i + 1] = b
        out[i + 2] = g
        out[i + 3] = a
    return bytes(out)


def q332_index(r: int, g: int, b: int) -> int:
    return (r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6)


def q332_default_rgb(idx: int):
    r = (idx & 0xE0) | 0x10
    g = ((idx << 3) & 0xE0) | 0x10
    b = ((idx << 6) & 0xC0) | 0x20
    return r, g, b


def build_idx8_clut_rgba(rgba_blob: bytes):
    pixels = len(rgba_blob) // 4
    idx_blob = bytearray(pixels)

    # Keep exact colors when possible.
    palette_map = {}
    palette = []
    exact_ok = True
    for p in range(pixels):
        off = p * 4
        rgba = (
            rgba_blob[off + 0],
            rgba_blob[off + 1],
            rgba_blob[off + 2],
            rgba_blob[off + 3],
        )
        idx = palette_map.get(rgba)
        if idx is None:
            if len(palette) >= 256:
                exact_ok = False
                break
            idx = len(palette)
            palette_map[rgba] = idx
            palette.append(rgba)
        idx_blob[p] = idx

    if exact_ok:
        clut = bytearray(256 * 4)
        for i, (r, g, b, a) in enumerate(palette):
            off = i * 4
            clut[off + 0] = r
            clut[off + 1] = g
            clut[off + 2] = b
            clut[off + 3] = a
        return bytes(idx_blob), bytes(clut)

    # Fall back to 3:3:2 quantization (always 256 colors).
    sums = [[0, 0, 0, 0, 0] for _ in range(256)]  # r,g,b,a,count
    for p in range(pixels):
        off = p * 4
        r = rgba_blob[off + 0]
        g = rgba_blob[off + 1]
        b = rgba_blob[off + 2]
        a = rgba_blob[off + 3]
        idx = q332_index(r, g, b)
        idx_blob[p] = idx
        sums[idx][0] += r
        sums[idx][1] += g
        sums[idx][2] += b
        sums[idx][3] += a
        sums[idx][4] += 1

    clut = bytearray(256 * 4)
    for idx in range(256):
        off = idx * 4
        count = sums[idx][4]
        if count > 0:
            clut[off + 0] = sums[idx][0] // count
            clut[off + 1] = sums[idx][1] // count
            clut[off + 2] = sums[idx][2] // count
            clut[off + 3] = sums[idx][3] // count
        else:
            r, g, b = q332_default_rgb(idx)
            clut[off + 0] = r
            clut[off + 1] = g
            clut[off + 2] = b
            clut[off + 3] = 0xFF

    return bytes(idx_blob), bytes(clut)


def emit_c_array(name: str, blob: bytes) -> str:
    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(blob), 12):
        chunk = blob[i:i + 12]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert RGBA PNG files to C arrays (RBGA + IDX8/CLUT32)")
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
        width, height, rgba_blob = decode_png_rgba(p)
        rbga_blob = rgba_to_rbga(rgba_blob)
        idx_blob, clut_blob = build_idx8_clut_rgba(rgba_blob)
        entries.append((symbol, width, height, rbga_blob, idx_blob, clut_blob, p))

    with out_path.open("w", encoding="ascii") as f:
        f.write("/* Auto-generated by tools/png_rgba_to_rbg_c.py */\n")
        f.write("/* Pixel layout is RBGA (R, B, G, A) by design. */\n\n")
        for symbol, width, height, rbga_blob, idx_blob, clut_blob, src_path in entries:
            f.write(f"/* Source: {src_path.as_posix()} */\n")
            f.write(f"const unsigned int {symbol}_width = {width}u;\n")
            f.write(f"const unsigned int {symbol}_height = {height}u;\n")
            f.write(emit_c_array(f"{symbol}_rbga", rbga_blob))
            f.write("\n")
            f.write(f"const unsigned int {symbol}_clut_entries = 256u;\n")
            f.write(emit_c_array(f"{symbol}_idx8", idx_blob))
            f.write("\n")
            f.write(emit_c_array(f"{symbol}_clut_rgba", clut_blob))
            f.write("\n\n")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        raise
