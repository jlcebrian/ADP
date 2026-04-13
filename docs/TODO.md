
# Interpreter

[x] Snapshot support
[x] Scroll attributes
[ ]	Fix 128K snapshot support (currently broken in CPC and Spectrum)
[ ]	CPC: Support more snapshot formats 
[x]	Add C64 snapshot support
[x]	Add MSX snapshot support
[ ] Amiga/DMG/DAT5: add support for half-brite mode (6 bitplanes)

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
[x] PROTECT (change windows dynamically)
[ ] Detect language (English/Spanish)
[ ] Timeout messing up history
[ ] MORE not working in Firfurcio

## Drawstring support

[x] Spectrum: Support TEXT command
[x] Spectrum: Fill attribute overwrite on unmodified pixels (Firfurcio)
[ ]	Spectrum: Paper/over issue in dwarf image (AO part2)
[ ]	Spectrum: Clear issue after dragon scene (AO part2)
[ ]	CPC: Support TEXT command
[ ]	CPC: Support BLOCK command
[ ]	CPC: Weird differences in SHADE and INK (Chichen vs editor)

[ ] Add support for multiple sentence conversations to PSI (Hobbit, PAWS A16)
[ ] Honor palette range as it appears in .DAT files (AD didn't use it?)
[x] Amiga/ST: Ask for disk # when there are multiple DDB files but no corresponding DATs
[ ] DOS: use fast VRAM copy in scroll
[x] DOS: faster DISPLAY implementation using page 3 as buffer
[ ] Fix LOAD/SAVE flow & make a compatible .SAV file format

# Tools

[ ] Write a compiler which supports version 1 files
[ ] DDB: Debugger (step by step, monitor flags & other state)

DMG

[ ] Write/add sound to dat
[ ] Support creation of EGA/CGA files and little endian .DATs
[ ] Debug CGA red/blue flag
[ ] Debug other potential flags in file
[ ] Link entry to another one from command line

DSK

[x] Add interactive mode
[x] Add help text for every subcommand
[x] mkdisk: Allow selection of disk type
[ ] FAT: Test & fix write support and mkdir/rmdir
[ ] FAT: Fix adding multiple files to disk (overwrites existing!)
[x] ADF: Disk creation support (w/special DSK tool syntax)
[x] ADF: Write file support
[x] ADF: File deletion support
[x] ADF: Mkdir/rmdir
[ ] CPC: Fails to identify master +3 disks
[ ] CPC: Disk creation support 
[ ] CPC: Write file support (should rename CPC to PCM?)
[ ] CPC: File deletion support
