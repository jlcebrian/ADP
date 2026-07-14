# C64 database extraction

Offline tooling that extracts DAAD databases from C64 program files by
running their unpackers inside a sandboxed 6502 (no hardware emulation
beyond fake raster registers, a synthesized IRQ dispatch and interrupt
masking). The goal is producing clean `.DDB` + `.CDG` fixture pairs; ADP
itself does not load packed scene releases.

- `cpu6502.py` — minimal NMOS 6502 core (documented + undocumented opcodes)
- `unpack64.py` — D64 directory/file reader
- `sweep64.py` — run every large PRG on the fixture disks until a valid
  database appears at $3880 (graphics footer at $CBED), then dump RAM
- `run_one.py` — single-file variant with tweakable IRQ/vector policy

Extraction from a dumped RAM image:
- `.DDB` = [$3880 .. eof), eof from the header
- `.CDG` = [minbas .. $CC00), minbas from the footer word at $CBED;
  loads flush against $CC00 (see the .CDG loader in ddb.cpp)

## Extraction completeness caveat

Dumping RAM the moment the database first validates can capture the
graphics mid-relocation (the loader may finish moving the CDG tail only
after later boot stages). Symptoms: garbage command tails in some
pictures (misplaced long lines, wrong colors). Prefer extracting from an
**in-game** emulator snapshot; when using the sandbox, verify the CDG by
rendering several pictures or by diffing against a snapshot taken at a
game prompt. The DDB itself relocates first and has not been observed
stale.
