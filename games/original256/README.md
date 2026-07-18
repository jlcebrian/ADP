# La Aventura Original — 256K remake

This is the primary source and distributable build for the 256K remake of
*La Aventura Original*. The source is redistributed with permission.

The project serves three purposes:

- a maintained release of the remake for Windows and Amiga HAM6;
- an end-to-end example using ADPC, DMG and DSK;
- a redistributable golden fixture for the ADP toolchain and playthrough tests.

## Build

Run `build.bat` from this directory after building the Win64 ADP tools. It:

1. compiles both game parts with ADPC;
2. creates the Windows SVGA and Amiga HAM graphics databases with DMG;
3. packages the Windows player directory and a bootable Amiga hard-disk ADF.

Outputs are written below `release/` and are intentionally not committed.

The committed files in `part1-ham/` and `part2-ham/` are the authoritative Amiga
artwork. The normal release build consumes them without regenerating them.
`build-ham.bat` is an explicit maintenance command for rebuilding the small set
of palette experiments from the true-colour sources; it is not part of
`build.bat`.

Run `export-ham-png.bat` to decode every authoritative IFF into ordinary RGB
PNGs under `inspection/part1/` and `inspection/part2/`. This is a direct HAM6
render for quality inspection: it does not rebuild palettes, dither, or modify
the source artwork.
The `reference/windows-previous/` directory preserves the last Windows
distribution supplied with the recovered project for historical comparison;
it is not used as build input.

Run `build-ham.bat --check` to verify that the committed override IFF files
match their truecolor sources and stored palettes.

## Artwork hierarchy

- `partN-truecolor/`: 2× source artwork used only for explicit overrides.
- `partN-svga/`: committed 240-color Windows conversion and DMG metadata.
- `partN-ham-palettes/`: palettes for the small explicit override set.
- `partN-ham/`: authoritative original HAM6 IFF files plus DMG metadata;
  files are never generally regenerated.

The original HAM palettes and command streams are preserved for image quality.
Only entries listed in `OVERRIDES` in `scripts/build-ham.py` are re-encoded.
Those palettes retain 15 of the 16 original base colors and replace one color
with black or white for the shared picture/text display. Override encoding
disables error-diffusion dithering to avoid high-frequency noise at 240x96.
