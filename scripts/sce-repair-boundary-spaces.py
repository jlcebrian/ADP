#!/usr/bin/env python3
"""
Repair visible DAAD spacing escapes in an already-converted UTF-8 source tree.

This is intended for cases where legacy DOS sources were previously converted
with the wrong rule `0x7F -> \\f`. The original DAAD text sources actually use:

  0x1E -> \\f   (inline spacing/layout control)
  0x7F -> \\s   (visible source-space marker at line boundaries)

Given an original CP437 source file and a later edited UTF-8 file derived from
it, this script aligns both files and copies only the boundary `\\s` / `\\f`
shape from the original into the edited file. The rest of the edited text is
left untouched.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path


ORIGINAL_ESCAPE_MAP = {
    0x10: "\\b",
    0x11: "\\k",
    0x18: "\\g",
    0x19: "\\t",
    0x1E: "\\f",
    0x7F: "\\s",
}

ESCAPE_TOKEN_RE = re.compile(r"\\[sf]")
LEADING_ESCAPES_RE = re.compile(r"^(?:\\[sf])+")
TRAILING_ESCAPES_RE = re.compile(r"(?:\\[sf])+$")


def convert_original_bytes(data: bytes) -> str:
    if data.endswith(b"\x1A"):
        data = data[:-1]
    parts: list[str] = []
    for b in data:
        if b in ORIGINAL_ESCAPE_MAP:
            parts.append(ORIGINAL_ESCAPE_MAP[b])
        elif b in (0x09, 0x0A, 0x0D):
            parts.append(chr(b))
        else:
            parts.append(bytes([b]).decode("cp437"))
    return "".join(parts)


def split_lines_keep_ends(text: str) -> list[str]:
    return text.splitlines(keepends=True)


def split_line_ending(line: str) -> tuple[str, str]:
    if line.endswith("\r\n"):
        return line[:-2], "\r\n"
    if line.endswith("\n") or line.endswith("\r"):
        return line[:-1], line[-1]
    return line, ""


def normalize_for_alignment(line: str) -> str:
    body, _ = split_line_ending(line)
    return ESCAPE_TOKEN_RE.sub(" ", body)


def tokenize_escapes(run: str) -> list[str]:
    return ESCAPE_TOKEN_RE.findall(run)


def replace_leading_run(current: str, original: str) -> tuple[str, bool]:
    current_match = LEADING_ESCAPES_RE.match(current)
    original_match = LEADING_ESCAPES_RE.match(original)
    if current_match is None or original_match is None:
        return current, False
    current_tokens = tokenize_escapes(current_match.group(0))
    original_tokens = tokenize_escapes(original_match.group(0))
    if len(current_tokens) != len(original_tokens):
        return current, False
    replacement = "".join(original_tokens)
    if replacement == current_match.group(0):
        return current, False
    return replacement + current[current_match.end():], True


def replace_trailing_run(current: str, original: str) -> tuple[str, bool]:
    current_match = TRAILING_ESCAPES_RE.search(current)
    original_match = TRAILING_ESCAPES_RE.search(original)
    if current_match is None or original_match is None:
        return current, False
    current_tokens = tokenize_escapes(current_match.group(0))
    original_tokens = tokenize_escapes(original_match.group(0))
    if len(current_tokens) != len(original_tokens):
        return current, False
    replacement = "".join(original_tokens)
    if replacement == current_match.group(0):
        return current, False
    return current[:current_match.start()] + replacement, True


def repair_line(original_line: str, current_line: str) -> tuple[str, bool]:
    original_body, current_ending = split_line_ending(original_line)[0], split_line_ending(current_line)[1]
    current_body, _ = split_line_ending(current_line)

    changed = False

    updated, did_change = replace_leading_run(current_body, original_body)
    current_body = updated
    changed |= did_change

    updated, did_change = replace_trailing_run(current_body, original_body)
    current_body = updated
    changed |= did_change

    return current_body + current_ending, changed


def align_lines(original_lines: list[str], current_lines: list[str]) -> list[tuple[int, int]]:
    original_norm = [normalize_for_alignment(line) for line in original_lines]
    current_norm = [normalize_for_alignment(line) for line in current_lines]
    matcher = difflib.SequenceMatcher(a=original_norm, b=current_norm, autojunk=False)
    pairs: list[tuple[int, int]] = []

    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            pairs.extend((i, j) for i, j in zip(range(i1, i2), range(j1, j2)))
            continue

        block_original = original_norm[i1:i2]
        block_current = current_norm[j1:j2]
        inner = difflib.SequenceMatcher(a=block_original, b=block_current, autojunk=False)
        for inner_tag, ii1, ii2, jj1, jj2 in inner.get_opcodes():
            original_range = range(i1 + ii1, i1 + ii2)
            current_range = range(j1 + jj1, j1 + jj2)
            count = min(ii2 - ii1, jj2 - jj1)
            if inner_tag in ("equal", "replace") and count > 0:
                pairs.extend((oi, cj) for oi, cj in zip(original_range, current_range))

    return pairs


def repair_file(original_path: Path, current_path: Path) -> tuple[str, int]:
    original_visible = convert_original_bytes(original_path.read_bytes())
    current_text = current_path.read_text(encoding="utf-8")

    original_lines = split_lines_keep_ends(original_visible)
    current_lines = split_lines_keep_ends(current_text)

    replacements = 0
    for original_index, current_index in align_lines(original_lines, current_lines):
        repaired, changed = repair_line(original_lines[original_index], current_lines[current_index])
        current_lines[current_index] = repaired
        if changed:
            replacements += 1

    return "".join(current_lines), replacements


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Repair boundary \\s/\\f escapes in an edited UTF-8 SCE/SCT file using the original DOS source."
    )
    parser.add_argument("original", help="Original CP437 source file")
    parser.add_argument("current", help="Edited UTF-8 source file to repair")
    parser.add_argument("output", nargs="?", help="Output path. Defaults to stdout unless --in-place is used.")
    parser.add_argument("--in-place", action="store_true", help="Rewrite the current file in place")
    args = parser.parse_args(argv)

    if args.in_place and args.output is not None:
        parser.error("--in-place cannot be used together with an explicit output path")

    original_path = Path(args.original)
    current_path = Path(args.current)

    try:
        repaired, replacements = repair_file(original_path, current_path)
    except OSError as exc:
        print(f"I/O error: {exc}", file=sys.stderr)
        return 1

    if args.in_place:
        output_path = current_path
    elif args.output is not None:
        output_path = Path(args.output)
    else:
        sys.stdout.write(repaired)
        print(f"{current_path}: repaired {replacements} boundary escape runs", file=sys.stderr)
        return 0

    try:
        output_path.write_text(repaired, encoding="utf-8", newline="")
    except OSError as exc:
        print(f"{output_path}: unable to write file: {exc}", file=sys.stderr)
        return 1

    print(f"{output_path}: repaired {replacements} boundary escape runs", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
