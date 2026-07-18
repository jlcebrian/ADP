#!/usr/bin/env python3
"""Explicitly regenerate selected Original256 HAM6 images from truecolor sources.

This is a maintenance tool, not a normal distributable-build step.  The committed
IFF files are the authoritative release artwork and must not be rewritten merely
by running build.bat.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[1]
DEFAULT_TOOL = ROOT / "tools" / "png2amiga" / "win64" / "png2amiga.exe"
EXPECTED_VERSION = "png2amiga 1.102.0.1186"
EXPECTED_SHA256 = "00f86dc565aae8164cbd91eea8ba859db207303cc78f4ca4ad8c494c35340612"
OVERRIDES = {1: ("012", "046", "047")}
PRESERVED_COUNT = 61
PRESERVED_SHA256 = "62d7a9f4058bb16f917a56756e6bfa39270d45935e57630c5fc8834d67757596"


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as source:
        header = source.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError(f"not a supported PNG: {path}")
    return struct.unpack(">II", header[16:24])


def run(command: list[str], capture: bool = False) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, cwd=PROJECT, text=True,
                               stdout=subprocess.PIPE if capture else None,
                               stderr=subprocess.PIPE if capture else None)
    if completed.returncode != 0:
        if capture:
            sys.stdout.write(completed.stdout)
            sys.stderr.write(completed.stderr)
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(command)}")
    return completed


def verify_tool(tool: Path) -> None:
    if not tool.is_file():
        raise FileNotFoundError(f"png2amiga not found: {tool}")
    digest = hashlib.sha256(tool.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError(f"unexpected png2amiga SHA-256: {digest}")
    completed = run([str(tool), "--version"], capture=True)
    version = (completed.stdout + completed.stderr).strip()
    if version != EXPECTED_VERSION:
        raise RuntimeError(f"unexpected png2amiga version: {version}")


def verify_preserved_originals() -> None:
    files: list[Path] = []
    for part in (1, 2):
        excluded = set(OVERRIDES.get(part, ()))
        files.extend(path for path in (PROJECT / f"part{part}-ham").glob("*.iff")
                     if path.stem not in excluded)
    files.sort(key=lambda path: path.relative_to(PROJECT).as_posix())
    digest = hashlib.sha256()
    for path in files:
        digest.update(path.relative_to(PROJECT).as_posix().encode("utf-8"))
        digest.update(path.read_bytes())
    if len(files) != PRESERVED_COUNT or digest.hexdigest() != PRESERVED_SHA256:
        raise RuntimeError("the authoritative original HAM IFF set has changed")


def converter_args(tool: Path, source: Path, output: Path) -> list[str]:
    width, height = png_size(source)
    return [str(tool), "--mode", "ham6", "--chipset", "ocs",
            "--dither", "none",
            "--width", str((width + 1) // 2), "--height", str((height + 1) // 2),
            str(source), "-o", str(output)]


def encode(tool: Path, source: Path, palette_path: Path, output: Path) -> None:
    if not palette_path.is_file():
        raise FileNotFoundError(f"missing HAM palette: {palette_path}")
    command = converter_args(tool, source, output)
    command[1:1] = ["--palette", str(palette_path)]
    command.append("--quiet")
    run(command)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--png2amiga", type=Path, default=DEFAULT_TOOL)
    parser.add_argument("--check", action="store_true",
                        help="regenerate overrides to temporary files and compare with committed IFFs")
    args = parser.parse_args()
    tool = args.png2amiga.resolve()
    verify_tool(tool)
    verify_preserved_originals()

    failures = 0
    with tempfile.TemporaryDirectory(prefix="original256-ham-") as temp_name:
        temp = Path(temp_name)
        for part in (1, 2):
            source_dir = PROJECT / f"part{part}-truecolor"
            palette_dir = PROJECT / f"part{part}-ham-palettes"
            output_dir = PROJECT / f"part{part}-ham"
            for stem in OVERRIDES.get(part, ()):
                source = source_dir / f"{stem}.png"
                palette_path = palette_dir / f"{stem}.json"
                destination = output_dir / f"{source.stem}.iff"
                generated = temp / f"part{part}-{source.stem}.iff" if args.check else destination
                encode(tool, source, palette_path, generated)
                if args.check:
                    if not destination.is_file() or generated.read_bytes() != destination.read_bytes():
                        print(f"FAIL part{part}/{source.stem}.iff differs")
                        failures += 1
                    else:
                        print(f"PASS part{part}/{source.stem}.iff")
                else:
                    print(f"Wrote {destination.relative_to(PROJECT)}")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1)
