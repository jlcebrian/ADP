#!/usr/bin/env python3
"""
Convert legacy DAAD SCE sources to editor-safe ASCII text.

This script decodes old CP437-based SCE files and rewrites embedded raw control
codes into textual forms that `adpc` understands:

  0x10 -> \\b   (CLS)
  0x11 -> \\k   (ANYKEY)
  0x18 -> \\g   (graphics on)
  0x19 -> \\t   (text mode)
  0x1E -> \\f   (legacy spacing control)
  0x7F -> \\s   (visible source-space marker)

The rest of the file is decoded from CP437 to UTF-8. CR/LF/TAB are preserved.
A trailing DOS EOF byte (0x1A) is dropped.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


ESCAPE_MAP = {
    0x10: "\\b",
    0x11: "\\k",
    0x18: "\\g",
    0x19: "\\t",
    0x1E: "\\f",
    0x7F: "\\s",
}


def convert_bytes(data: bytes) -> tuple[str, dict[int, int]]:
    counts = {code: 0 for code in sorted(ESCAPE_MAP)}
    if data.endswith(b"\x1A"):
        data = data[:-1]

    parts: list[str] = []
    for b in data:
        if b in ESCAPE_MAP:
            parts.append(ESCAPE_MAP[b])
            counts[b] += 1
        elif b in (0x09, 0x0A, 0x0D):
            parts.append(chr(b))
        else:
            parts.append(bytes([b]).decode("cp437"))
    return "".join(parts), counts


def build_report(path: Path, counts: dict[int, int]) -> str:
    used = [f"0x{code:02X}={counts[code]}" for code in sorted(counts) if counts[code] > 0]
    suffix = ", ".join(used) if used else "no raw DAAD control bytes found"
    return f"{path}: {suffix}"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Convert legacy CP437 SCE files to UTF-8 with ASCII-safe DAAD escapes."
    )
    parser.add_argument("input", help="Input .sce file")
    parser.add_argument(
        "output",
        nargs="?",
        help="Output file. If omitted, writes converted text to stdout.",
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="Rewrite the input file in place as UTF-8 ASCII-safe text.",
    )
    args = parser.parse_args(argv)

    if args.in_place and args.output is not None:
        parser.error("--in-place cannot be used together with an explicit output path")

    input_path = Path(args.input)
    try:
        data = input_path.read_bytes()
    except OSError as exc:
        print(f"{input_path}: unable to read file: {exc}", file=sys.stderr)
        return 1

    converted, counts = convert_bytes(data)

    if args.in_place:
        output_path = input_path
    elif args.output is not None:
        output_path = Path(args.output)
    else:
        sys.stdout.write(converted)
        if not converted.endswith("\n"):
            sys.stdout.write("\n")
        print(build_report(input_path, counts), file=sys.stderr)
        return 0

    try:
        output_path.write_text(converted, encoding="utf-8", newline="")
    except OSError as exc:
        print(f"{output_path}: unable to write file: {exc}", file=sys.stderr)
        return 1

    print(build_report(input_path, counts), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
