#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


KEYS = [
    "AUTO",
    "TRIANGLE",
    "CIRCLE",
    "CROSS",
    "SQUARE",
    "UP",
    "DOWN",
    "LEFT",
    "RIGHT",
    "L1",
    "L2",
    "L3",
    "R1",
    "R2",
    "R3",
    "SELECT",
    "START",
]


def parse_int(value, default=-1):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def parse_int_pair(value):
    if value is None:
        return None
    parts = [p for p in re.split(r"[,\s]+", value.strip()) if p]
    if len(parts) < 2:
        return None
    try:
        return int(parts[0]), int(parts[1])
    except ValueError:
        return None


def load_map(path):
    mapping = {}
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#") or line.startswith(";") or line.startswith("//"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        mapping[key.strip().upper()] = value.strip()
    return mapping


def get_hotkey_pos(mapping, key):
    x = -1
    y = -1
    pair = None
    pair_key = f"HOTKEY_{key}"
    if pair_key in mapping:
        pair = parse_int_pair(mapping.get(pair_key))
    if pair is not None:
        x, y = pair
    else:
        x_key = f"HOTKEY_{key}_X"
        y_key = f"HOTKEY_{key}_Y"
        if x_key in mapping:
            x = parse_int(mapping.get(x_key), x)
        if y_key in mapping:
            y = parse_int(mapping.get(y_key), y)
        alt_x_key = f"{key}_X"
        alt_y_key = f"{key}_Y"
        if alt_x_key in mapping:
            x = parse_int(mapping.get(alt_x_key), x)
        if alt_y_key in mapping:
            y = parse_int(mapping.get(alt_y_key), y)
    return x, y


def get_console_pos(mapping, base_key):
    x = -1
    y = -1
    if base_key in mapping:
        pair = parse_int_pair(mapping.get(base_key))
        if pair is not None:
            return pair
    x_key = f"{base_key}_X"
    y_key = f"{base_key}_Y"
    if x_key in mapping:
        x = parse_int(mapping.get(x_key), x)
    if y_key in mapping:
        y = parse_int(mapping.get(y_key), y)
    return x, y


def main():
    parser = argparse.ArgumentParser(description="Generate splash layout C file from INI-style layout.")
    parser.add_argument("-LayoutPath", dest="layout_path", required=True, help="Path to splash_layout.ini")
    parser.add_argument("-OutC", dest="out_c", required=True, help="Output C file path")
    args = parser.parse_args()

    mapping = load_map(args.layout_path)
    positions = [get_hotkey_pos(mapping, key) for key in KEYS]
    console_info = get_console_pos(mapping, "CONSOLE_INFO")
    console_temp = get_console_pos(mapping, "CONSOLE_TEMP")

    out_lines = []
    out_lines.append('#include "splash_layout.h"')
    out_lines.append("")
    out_lines.append("const SplashTextPos splash_hotkey_positions[17] = {")
    for i, (x, y) in enumerate(positions):
        comma = "," if i < len(positions) - 1 else ""
        out_lines.append(f"    {{ {x}, {y} }}{comma}")
    out_lines.append("};")
    out_lines.append("")
    out_lines.append(f"const SplashTextPos splash_console_info_pos = {{ {console_info[0]}, {console_info[1]} }};")
    out_lines.append(f"const SplashTextPos splash_console_temp_pos = {{ {console_temp[0]}, {console_temp[1]} }};")
    out_lines.append("")

    Path(args.out_c).write_text("\n".join(out_lines), encoding="ascii")


if __name__ == "__main__":
    main()
