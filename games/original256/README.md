# La Aventura Original remake

This is the primary source and distributable build for the remake of
*La Aventura Original*. The source is redistributed with permission.

The project serves three purposes:

- a maintained release of the remake for Windows and Amiga HAM6;
- an end-to-end example using ADPC, DMG and DSK;
- a redistributable golden fixture for the ADP toolchain and playthrough tests.

## Build

On Linux and macOS, run `make` from this directory. It builds the host ADP
tools via the top-level makefile if they are missing, then:

1. compiles both game parts with ADPC;
2. creates the SDL SVGA and Amiga HAM graphics databases with DMG;
3. packages the Windows, Linux and macOS player directories, a bootable
   Amiga hard-disk ADF, and (when `emcc` is on the PATH) the emscripten
   web build.

The Windows and macOS distributables use the prebuilt players committed
under `daad-ready/dist/ASSETS/`; the Linux player is the canonical binary
produced by `Makefile-linux`. `make web` builds the web distributable on
its own and requires an activated emsdk. See the Makefile header for the
maintenance targets (`inspect`, `ham`, `ham-check`).

On Windows, run `build.bat` after building the Win64 ADP tools. It performs
the same steps for the Windows and Amiga distributables only.

Outputs are written below `release/` and are intentionally not committed.

The committed files in `part1-ham/` and `part2-ham/` are the authoritative Amiga
artwork. The normal release build consumes them without regenerating them.
`build-ham.bat` is an explicit maintenance command for rebuilding the small set
of palette experiments from the true-colour sources; it is not part of
`build.bat`.

Run `make inspect` (or `export-ham-png.bat` on Windows) to decode every
authoritative IFF into ordinary RGB PNGs under `inspection/part1/` and
`inspection/part2/`. This is a direct HAM6
render for quality inspection: it does not rebuild palettes, dither, or modify
the source artwork.
The `reference/windows-previous/` directory preserves the last Windows
distribution supplied with the recovered project for historical comparison;
it is not used as build input.

Run `make ham-check` (or `build-ham.bat --check` on Windows) to verify that
the committed override IFF files match their truecolor sources and stored
palettes. Note that the HAM re-encoding steps invoke the pinned
`tools/png2amiga/win64/png2amiga.exe`, so on Linux/macOS they need a
compatible interpreter for it (or `--png2amiga` pointing at a native build).

## Artwork hierarchy

- `part1-amiga.scr`: the boot loading screen, byte-for-byte the raw 16-color
  SCR from the 1989 Amiga release. It is kept in the raw format (not ILBM)
  because the host player and test tooling render raw SCR only; the Amiga
  player displays it on the HAM6 screen through its base palette.
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
