
# Interpreter

[x] Snapshot support
[x] Scroll attributes
[ ]	Fix 128K snapshot support (currently broken in CPC and Spectrum)
[ ]	CPC: Support more snapshot formats 
[x]	Add C64 snapshot support
[x]	Add MSX snapshot support
[x] Amiga/DMG/DAT5: add support for half-brite mode (6 bitplanes)
[ ] Amiga/DMG/DAT5: add support for HAM mode w/ split screen

## PAWS

[x] 128K support
[x] UDGs
[x] INVERSE support
[x] ANYKEY shows message at bottom of screen
[x] Fancy cursor support in INPUT
[x] INVEN
[x] DESC: Clear screen, LINE
[x] DESC: Honor GRAPHIC mode, add visited flag
[x] FLASH support
[x] Optional verb/noun match (PAWS A16)
[x] MODE: More... toggle
[x] Fixed 544-byte live/SAVE/RAMSAVE state
[x] PAW-specific MODE/GRAPHIC/LINE/TIME/INPUT packing
[x] PROTECT and flag-39 single-screen protected scrolling
[x] 50 Hz PAUSE/TIME semantics with deterministic fast tests
[x] Permanent/temporary text attributes and multi-byte controls
[x] Firfurcio full-completion regression scenario
[x] Fuse-derived Firfurcio startup/scroll/font/colour golden scenario
[ ] Reconcile PAWS graphics-format command interpretation with emulator corpus
[ ] Detect language (English/Spanish)
[ ] Timeout messing up history
[ ] Verify MORE timeout and state restoration against original PAW
[ ] Port the exact PAW parser state machine
[ ] Add PAW 128K graphics page mapping and PC PDB loading

## Drawstring support

[x] Spectrum: Support TEXT command
[x] Spectrum: Fill attribute overwrite on unmodified pixels (Firfurcio)
[ ]	Spectrum: Paper/over issue in dwarf image (AO part2)
[ ]	Spectrum: Clear issue after dragon scene (AO part2)
[ ]	CPC: Support TEXT command
[ ]	CPC: Support BLOCK command
[ ]	CPC: Weird differences in SHADE and INK (Chichen vs editor)

[ ] Add support for multiple sentence conversations to PSI (Hobbit, PAWS A16)
[x] Honor palette range as it appears in .DAT files (AD didn't use it?)
[x] Amiga/ST: Ask for disk # when there are multiple DDB files but no corresponding DATs
[x] DOS: use fast VRAM copy in scroll
[x] DOS: faster DISPLAY implementation using page 3 as buffer
[x] DOS: add fast direct IndexedX conversion/decompression paths for old graphics
[x] Fix LOAD/SAVE flow & make a compatible .SAV file format

# Tools

[x] Write a compiler which supports version 1 files
[ ] DDB: Debugger (step by step, monitor flags & other state)

DMG

[x] Write/add sound to dat
[x] Support creation of EGA/CGA files and little endian .DATs
[x] Debug CGA red/blue flag
[ ] Debug other potential flags in file
[x] Link entry to another one from command line
[ ] Allow EGA/CGA palettes to be imported from JSON

DSK

[x] Add interactive mode
[x] Add help text for every subcommand
[x] mkdisk: Allow selection of disk type
[x] FAT: Test & fix write support and mkdir/rmdir
[x] FAT: Fix adding multiple files to disk (overwrites existing!)
[x] ADF: Disk creation support (w/special DSK tool syntax)
[x] ADF: Write file support
[x] ADF: File deletion support
[x] ADF: Mkdir/rmdir
[x] CPC: Fails to identify master +3 disks
[x] CPC: Disk creation support 
[x] CPC: Write file support (should rename CPC to PCM?)
[x] CPC: File deletion support
