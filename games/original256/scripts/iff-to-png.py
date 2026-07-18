#!/usr/bin/env python3
"""Render Amiga ILBM/HAM6 files to ordinary RGB PNG files."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


def chunks(data: bytes):
    if data[:4] != b"FORM" or data[8:12] != b"ILBM":
        raise ValueError("not an ILBM FORM")
    end = min(len(data), 8 + struct.unpack_from(">I", data, 4)[0])
    pos = 12
    while pos + 8 <= end:
        name, size = struct.unpack_from(">4sI", data, pos)
        start = pos + 8
        yield name, data[start:start + size]
        pos = start + size + (size & 1)


def byterun1(data: bytes, size: int) -> bytes:
    result = bytearray()
    pos = 0
    while len(result) < size and pos < len(data):
        control = data[pos]
        pos += 1
        if control <= 127:
            count = control + 1
            result.extend(data[pos:pos + count])
            pos += count
        elif control >= 129:
            count = 257 - control
            if pos >= len(data):
                raise ValueError("truncated ByteRun1 repeat")
            result.extend(data[pos:pos + 1] * count)
            pos += 1
    if len(result) != size:
        raise ValueError("invalid ByteRun1 body")
    return bytes(result)


def decode(path: Path) -> tuple[int, int, bytes]:
    found = {name: payload for name, payload in chunks(path.read_bytes())}
    if b"BMHD" not in found or b"CMAP" not in found or b"BODY" not in found:
        raise ValueError("ILBM is missing BMHD, CMAP, or BODY")
    bmhd = found[b"BMHD"]
    width, height = struct.unpack_from(">HH", bmhd)
    planes, masking, compression = bmhd[8:11]
    if masking != 0 or not 1 <= planes <= 8:
        raise ValueError("unsupported ILBM masking or plane count")
    row_bytes = ((width + 15) // 16) * 2
    raw_size = row_bytes * height * planes
    body = found[b"BODY"]
    if compression == 1:
        body = byterun1(body, raw_size)
    elif compression != 0 or len(body) < raw_size:
        raise ValueError("unsupported or truncated ILBM BODY")
    cmap = found[b"CMAP"]
    palette = [tuple(cmap[n:n + 3]) for n in range(0, len(cmap) - 2, 3)]
    camg = struct.unpack_from(">I", found.get(b"CAMG", b"\0\0\0\0"))[0]
    ham = bool(camg & 0x0800)
    if ham and planes != 6:
        raise ValueError("HAM rendering currently requires HAM6")

    rgb = bytearray(width * height * 3)
    for y in range(height):
        held = palette[0]
        row_base = y * row_bytes * planes
        for x in range(width):
            value = 0
            mask = 1 << (7 - (x & 7))
            byte = x >> 3
            for plane in range(planes):
                if body[row_base + plane * row_bytes + byte] & mask:
                    value |= 1 << plane
            if ham:
                command, nibble = value >> 4, (value & 15) * 17
                if command == 0:
                    held = palette[value & 15]
                elif command == 1:
                    held = (held[0], held[1], nibble)
                elif command == 2:
                    held = (nibble, held[1], held[2])
                else:
                    held = (held[0], nibble, held[2])
                color = held
            else:
                if value >= len(palette):
                    raise ValueError(f"palette index {value} is not present")
                color = palette[value]
            offset = (y * width + x) * 3
            rgb[offset:offset + 3] = bytes(color)
    return width, height, bytes(rgb)


def png_chunk(name: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload))


def save_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
                     png_chunk(b"IDAT", zlib.compress(rows, 9)) + png_chunk(b"IEND", b""))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="IFF file(s) or directories")
    parser.add_argument("-o", "--output", type=Path, required=True, help="output directory")
    args = parser.parse_args()
    files: list[Path] = []
    for source in args.inputs:
        files.extend(sorted(source.glob("*.iff"))) if source.is_dir() else files.append(source)
    for source in files:
        try:
            width, height, rgb = decode(source)
            destination = args.output / f"{source.stem}.png"
            save_png(destination, width, height, rgb)
            print(f"{source} -> {destination} ({width}x{height})")
        except ValueError as error:
            raise SystemExit(f"{source}: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
