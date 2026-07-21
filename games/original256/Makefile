# La Aventura Original — 256K remake, distributable build (Linux/macOS host).
# Unix equivalent of build.bat; see README.md for the artwork pipeline.
#
#   make            build the windows, linux, macos and amiga distributables
#                   under release/ (plus web when emcc is on the PATH)
#   make web        emscripten build (requires an activated emsdk)
#   make tools      (re)build the host ADP tools via the top-level makefile
#   make inspect    render the authoritative HAM IFFs to PNG under inspection/
#   make ham        regenerate the OVERRIDES palette experiments (maintenance)
#   make ham-check  verify committed override IFFs against their sources
#   make amiga-debug  master debug-player DD floppies for scripts/amiga-debug.sh
#   make clean

ROOT    := ../..
PYTHON  ?= python3
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
HOST_MK := Makefile-osx
else
HOST_MK := Makefile-linux
endif

ADPC := $(ROOT)/out/adpc
DMG  := $(ROOT)/out/dmg
DSK  := $(ROOT)/out/dsk

ASSETS       := $(ROOT)/daad-ready/dist/ASSETS
WIN_PLAYER   := $(ASSETS)/WINDOWS_EXPERIMENTAL/adp-player.exe
LINUX_PLAYER := $(ASSETS)/LINUX_EXPERIMENTAL/player
MACOS_APP    := $(ASSETS)/MACOS_EXPERIMENTAL/ADP-EXPERIMENTAL.app
AMIGA_PLAYER ?= $(ASSETS)/AMIGA_EXPERIMENTAL/ADP.EXE
# The DOS player and SETUP are the normal Makefile-dos build artifacts (built
# with the Watcom toolchain). Never source these from daad-ready/dist — those
# are a distribution handed to a separate project and are not guaranteed current.
DOS_PLAYER   ?= $(ROOT)/out/adp32.exe
DOS_SETUP    ?= $(ROOT)/out/setup.exe

TR := --tr U+00DA=35 --tr U+00C1=36 --tr U+00D3=24 --tr U+00CD=37 --tr U+00C9=38

RELEASE  := release
SVGA     := $(RELEASE)/data-svga
DOS      := $(RELEASE)/dos
WIN      := $(RELEASE)/windows
LINUX    := $(RELEASE)/linux
MACOS    := $(RELEASE)/macos
AMIGA    := $(RELEASE)/amiga-ham
AMIGAHD  := $(RELEASE)/amiga-hd
AMIGADD1 := $(RELEASE)/amiga-dd1
AMIGADD2 := $(RELEASE)/amiga-dd2
ADF_DD1  := $(RELEASE)/original256-amiga-ham-disk1.adf
ADF_DD2  := $(RELEASE)/original256-amiga-ham-disk2.adf
AMIGAZIP := $(RELEASE)/original256-amiga.zip
WEB      := $(RELEASE)/web
WEBSTAGE := $(ROOT)/web-games/original256
WEBOUT   := $(ROOT)/out/web/original256

# Release web builds have no debug info; override with WEB_DEBUG=1.
WEB_DEBUG ?= 0

SCT_FILES := $(wildcard *.sct)
SVGA1_SRC := $(filter-out %.dat %.dmgcache %.scr,$(wildcard part1-svga/*))
SVGA2_SRC := $(filter-out %.dat %.dmgcache,$(wildcard part2-svga/*))
HAM1_SRC  := $(filter-out %.dat %.dmgcache,$(wildcard part1-ham/*))
HAM2_SRC  := $(filter-out %.dat %.dmgcache,$(wildcard part2-ham/*))

# The SDL data set is shared by the windows, linux, macos and web releases.
SVGA_FILES := part1.ddb part2.ddb part1.dat part2.dat \
              part1.chr part2.chr part1.scr part1.cfg
SVGA_ALL   := $(addprefix $(SVGA)/,$(SVGA_FILES))

AMIGADD1_FILES := part1.ddb part1.dat part1.chr part1.scr part1.cfg ADP.EXE s/startup-sequence
AMIGADD1_ALL   := $(addprefix $(AMIGADD1)/,$(AMIGADD1_FILES))
AMIGADD2_FILES := part2.ddb part2.dat part2.chr
AMIGADD2_ALL   := $(addprefix $(AMIGADD2)/,$(AMIGADD2_FILES))

WIN_ALL   := $(addprefix $(WIN)/,$(SVGA_FILES)) $(WIN)/ad.exe
LINUX_ALL := $(addprefix $(LINUX)/,$(SVGA_FILES)) $(LINUX)/adp

# The DOS release ships two graphics databases: the 640x400 SVGA (VESA) set
# and the 320x200 VGA (Mode-X) set. Both are INDEXED DAT5 — the only 256-colour
# format the DOS player decodes — so they cannot reuse the SDL data set's
# planar8 .dat. Everything else (database, charset, loading screen, config) is
# shared with the SVGA data set. Text uses the game's own part1.chr charset,
# which the SVGA path scales 2x (the same "scaled original font" the desktop
# builds use); no separate font file is shipped.
DOS_SHARED := part1.ddb part2.ddb part1.chr part2.chr part1.scr part1.cfg
DOS_FILES  := $(DOS_SHARED) part1.dat part2.dat part1.vga part2.vga \
              part1.xgs ADP.EXE SETUP.EXE README.TXT
DOS_ALL    := $(addprefix $(DOS)/,$(DOS_FILES))

HAVE_EMCC := $(shell command -v emcc 2>/dev/null)
# The DOS package needs the Watcom-built player/SETUP; include it in the default
# build only when those artifacts are already present (like web + emcc), so a
# plain `make` on a host without the DOS toolchain still succeeds.
HAVE_DOS := $(and $(wildcard $(DOS_PLAYER)),$(wildcard $(DOS_SETUP)))

.DEFAULT_GOAL := all
.PHONY: all windows linux macos amiga amiga-hd dos web tools inspect ham ham-check clean

all: windows linux macos amiga amiga-hd $(if $(HAVE_DOS),dos) $(if $(HAVE_EMCC),web)
	$(if $(HAVE_DOS),,@echo "Note: DOS player/SETUP not built, DOS build skipped. Build them with 'dos-toolchain wmake -h -f Makefile-dos MODE=32' and 'MODE=16', then 'make dos'.")
	$(if $(HAVE_EMCC),,@echo "Note: emcc not found, web build skipped. Activate emsdk and run 'make web'.")
	@echo "Distributables written to $(CURDIR)/$(RELEASE)"

# ------------------------------------------------------------------ SDL data

$(SVGA)/part1.ddb: part1.sce $(SCT_FILES) | $(ADPC)
	@mkdir -p $(SVGA)
	$(ADPC) --target ibmpc $(TR) $< $@

$(SVGA)/part2.ddb: part2.sce $(SCT_FILES) | $(ADPC)
	@mkdir -p $(SVGA)
	$(ADPC) --target ibmpc $(TR) $< $@

$(SVGA)/part1.dat: $(SVGA1_SRC) | $(DMG)
	@mkdir -p $(SVGA)
	$(DMG) create $@ part1-svga -format=dat5 -mode=planar8 -width=480 -height=192 -remap=16-255 -screen=640x400 -2x=1
	$(DMG) test $@ >/dev/null

$(SVGA)/part2.dat: $(SVGA2_SRC) | $(DMG)
	@mkdir -p $(SVGA)
	$(DMG) create $@ part2-svga -format=dat5 -mode=planar8 -remap=16-255 -screen=640x400 -2x=1
	$(DMG) test $@ >/dev/null

$(SVGA)/%.chr: %.chr
	@mkdir -p $(SVGA)
	cp $< $@

$(SVGA)/part1.scr: part1-svga/part1.scr
	@mkdir -p $(SVGA)
	cp $< $@

$(SVGA)/part1.cfg: windows.cfg
	@mkdir -p $(SVGA)
	cp $< $@

# ------------------------------------------------------------------- windows

windows: $(WIN_ALL)

$(addprefix $(WIN)/,$(SVGA_FILES)): $(WIN)/%: $(SVGA)/%
	@mkdir -p $(WIN)
	cp $< $@

$(WIN)/ad.exe: $(WIN_PLAYER)
	@mkdir -p $(WIN)
	cp $< $@

# --------------------------------------------------------------------- linux

linux: $(LINUX_ALL)

$(addprefix $(LINUX)/,$(SVGA_FILES)): $(LINUX)/%: $(SVGA)/%
	@mkdir -p $(LINUX)
	cp $< $@

$(LINUX)/adp: $(LINUX_PLAYER)
	@mkdir -p $(LINUX)
	cp $< $@
	chmod +x $@

# --------------------------------------------------------------------- macos
# Packages the prebuilt player bundle; the game data lives in
# Contents/Resources, where the player chdirs on startup.

macos: $(SVGA_ALL)
	@rm -rf "$(MACOS)/Aventura Original.app"
	@mkdir -p $(MACOS)
	cp -R $(MACOS_APP) "$(MACOS)/Aventura Original.app"
	cp $(SVGA_ALL) "$(MACOS)/Aventura Original.app/Contents/Resources/"

# --------------------------------------------------------------------- amiga
# The distribution is the two-disk double-density set (bundled as a zip),
# which stock OCS hardware boots from disk 1 and swaps to disk 2 on the
# player's insert-disk prompt (PARTS/DISKS keys in part1.cfg). No high-density
# ADF is produced: no stock Amiga shipped an HD floppy drive, and the machines
# that could read one run the game from hard disk instead.

amiga: $(ADF_DD1) $(ADF_DD2) $(AMIGAZIP)

# Debug counterparts of the two-disk set: same data, but the player is the
# uncrunched CONFIG=debug build so scripts/amiga-debug.sh can attach gdb:
#   scripts/amiga-debug.sh --adf release/debug-disk1.adf release/debug-disk2.adf
DEBUG_PLAYER ?= $(ROOT)/out/amiga/adp-debug.exe
AMIGADBG1    := $(RELEASE)/amiga-dbg1
ADF_DBG1     := $(RELEASE)/debug-disk1.adf
ADF_DBG2     := $(RELEASE)/debug-disk2.adf

amiga-debug: $(ADF_DBG1) $(ADF_DBG2)

$(ADF_DBG1): $(AMIGADD1_ALL) $(DEBUG_PLAYER) | $(DSK)
	@rm -rf $(AMIGADBG1)
	@cp -r $(AMIGADD1) $(AMIGADBG1)
	cp $(DEBUG_PLAYER) $(AMIGADBG1)/ADP.EXE
	$(DSK) create -b -r $@ dd $(AMIGADBG1)
	$(DSK) setvol $@ Original1

$(ADF_DBG2): $(ADF_DD2)
	cp $< $@

$(DEBUG_PLAYER):
	@echo "Debug player not built; run: amiga-toolchain make -f Makefile-amiga CONFIG=debug"
	@exit 1

$(ADF_DD1): $(AMIGADD1_ALL) | $(DSK)
	$(DSK) create -b -r $@ dd $(AMIGADD1)
	$(DSK) setvol $@ Original1

$(ADF_DD2): $(AMIGADD2_ALL) | $(DSK)
	$(DSK) create -r $@ dd $(AMIGADD2)
	$(DSK) setvol $@ Original2

# The redistributable Amiga bundle: both floppies of the swap set in one zip.
$(AMIGAZIP): $(ADF_DD1) $(ADF_DD2)
	@rm -f $@
	cd $(RELEASE) && zip -q -X $(notdir $@) $(notdir $(ADF_DD1)) $(notdir $(ADF_DD2))

# Amiga HD install folder (development/testing, not distributed). fs-uae mounts
# it as a directory hard drive — far faster than floppy emulation — and the host
# ddb tool reads it directly (the amiga transcript fixtures point at its
# part1.ddb/part2.ddb). Same HAM6 data as the floppy set, but a single volume
# with everything present, so no disk-swap/part-selector flow. Built from the
# amiga-ham staging dir.
AMIGAHD_DATA := part1.ddb part2.ddb part1.dat part2.dat part1.chr part2.chr part1.scr ADP.EXE
AMIGAHD_ALL  := $(addprefix $(AMIGAHD)/,$(AMIGAHD_DATA)) $(AMIGAHD)/part1.cfg $(AMIGAHD)/s/startup-sequence

amiga-hd: $(AMIGAHD_ALL)

$(addprefix $(AMIGAHD)/,$(AMIGAHD_DATA)): $(AMIGAHD)/%: $(AMIGA)/%
	@mkdir -p $(AMIGAHD)
	cp $< $@

$(AMIGAHD)/part1.cfg: amiga.cfg
	@mkdir -p $(AMIGAHD)
	cat $< > $@
	printf 'FILES=part1.ddb,part1.dat,part1.chr,part1.scr,part1.cfg,part2.ddb,part2.dat,part2.chr\n' >> $@

$(AMIGAHD)/s/startup-sequence:
	@mkdir -p $(dir $@)
	printf 'ADP.EXE\n' > $@

$(AMIGADD1)/part1.ddb $(AMIGADD1)/part1.dat $(AMIGADD1)/part1.chr $(AMIGADD1)/part1.scr $(AMIGADD1)/ADP.EXE: $(AMIGADD1)/%: $(AMIGA)/%
	@mkdir -p $(AMIGADD1)
	cp $< $@

$(AMIGADD1)/part1.cfg: amiga.cfg
	@mkdir -p $(AMIGADD1)
	cat $< > $@
	printf 'PARTS=2\nDISKS=2\n' >> $@
	printf 'FILES=part1.ddb,part1.dat,part1.chr,part1.scr,part1.cfg\n' >> $@
	printf 'FILES2=part2.ddb,part2.dat,part2.chr\n' >> $@

$(AMIGADD1)/s/startup-sequence:
	@mkdir -p $(dir $@)
	printf 'ADP.EXE\n' > $@

$(AMIGADD2)/%: $(AMIGA)/%
	@mkdir -p $(AMIGADD2)
	cp $< $@

$(AMIGA)/part1.ddb: part1.sce $(SCT_FILES) | $(ADPC)
	@mkdir -p $(AMIGA)
	$(ADPC) --target amiga $(TR) $< $@

$(AMIGA)/part2.ddb: part2.sce $(SCT_FILES) | $(ADPC)
	@mkdir -p $(AMIGA)
	$(ADPC) --target amiga $(TR) $< $@

$(AMIGA)/part1.dat: $(HAM1_SRC) | $(DMG)
	@mkdir -p $(AMIGA)
	$(DMG) create $@ part1-ham -format=dat5 -mode=ham6 -width=240 -height=96 -screen=320x200
	$(DMG) test $@ >/dev/null

$(AMIGA)/part2.dat: $(HAM2_SRC) | $(DMG)
	@mkdir -p $(AMIGA)
	$(DMG) create $@ part2-ham -format=dat5 -mode=ham6 -screen=320x200
	$(DMG) test $@ >/dev/null

$(AMIGA)/%.chr: %.chr
	@mkdir -p $(AMIGA)
	cp $< $@

# HAM6 boot loading screen, generated from the desktop presentation art
# (the 1989 release's original 16-color screen is kept as part1-amiga.scr).
# Regenerate with `make part1-amiga-ham.iff REGEN=1` (requires png2amiga).
$(AMIGA)/part1.scr: part1-amiga-ham.iff
	@mkdir -p $(AMIGA)
	cp $< $@

ifdef REGEN
part1-amiga-ham.iff: part1-svga/part1.scr
	python3 -c "from PIL import Image; Image.open('$<').convert('RGB').save('compile/present.png')"
	png2amiga compile/present.png --mode ham6 --width 320 --height 200 --best -o $@
endif

$(AMIGA)/ADP.EXE: $(AMIGA_PLAYER)
	@mkdir -p $(AMIGA)
	cp $< $@

# ----------------------------------------------------------------------- dos
# 32-bit DOS distributable (386+, PMODE/W). The player (out/adp32.exe) and the
# 16-bit SETUP configurator (out/setup.exe) are the normal Makefile-dos build
# artifacts; build them first with the Watcom toolchain. The host DMG/ADPC tools
# build the data. The database, charset, loading screen and config are copied
# from the SVGA data set; only the graphics databases are rebuilt here in the
# DOS-native INDEXED format.
# A game exposing more than one graphics mode requires the user to run SETUP
# once (the player refuses to start otherwise), so no ADPSETUP.CFG is shipped.

dos: $(DOS_ALL)

# 640x400 SVGA graphics (INDEXED / VESA; the SVGA path scales part1.chr 2x)
$(DOS)/part1.dat: $(SVGA1_SRC) | $(DMG)
	@mkdir -p $(DOS)
	$(DMG) create $@ part1-svga -format=dat5 -mode=indexed -width=480 -height=192 -remap=16-255 -screen=640x400 -2x=1
	$(DMG) test $@ >/dev/null

$(DOS)/part2.dat: $(SVGA2_SRC) | $(DMG)
	@mkdir -p $(DOS)
	$(DMG) create $@ part2-svga -format=dat5 -mode=indexed -remap=16-255 -screen=640x400 -2x=1
	$(DMG) test $@ >/dev/null

# 320x200 VGA graphics (INDEXED / Mode-X, half resolution)
$(DOS)/part1.vga: $(SVGA1_SRC) | $(DMG)
	@mkdir -p $(DOS)
	$(DMG) create $@ part1-svga -format=dat5 -mode=indexed -width=240 -height=96 -remap=16-255 -screen=320x200
	$(DMG) test $@ >/dev/null

$(DOS)/part2.vga: $(SVGA2_SRC) | $(DMG)
	@mkdir -p $(DOS)
	$(DMG) create $@ part2-svga -format=dat5 -mode=indexed -width=240 -height=96 -remap=16-255 -screen=320x200
	$(DMG) test $@ >/dev/null

# Database, charset, config and the SVGA loading screen (part1.scr, 640x400):
# identical to the SVGA data set.
$(addprefix $(DOS)/,$(DOS_SHARED)): $(DOS)/%: $(SVGA)/%
	@mkdir -p $(DOS)
	cp $< $@

# VGA loading screen: the shared part1.scr is 640x400 and would overflow a
# 320x200 buffer (and the 16-bit player's PCX loader), so the 256-colour VGA
# mode gets its own 320x200 screen. The player picks it up by extension for
# ScreenMode_VGA (GetModeSpecificIntroScreen, .xgs = Mode-X graphic screen);
# downscaled from the SVGA art.
$(DOS)/part1.xgs: part1-svga/part1.scr
	@mkdir -p $(DOS)
	$(PYTHON) -c "from PIL import Image; Image.open('$<').convert('RGB').resize((320,200)).quantize(colors=256).save('$@', format='PCX')"

$(DOS)/ADP.EXE: $(DOS_PLAYER)
	@mkdir -p $(DOS)
	cp $< $@

$(DOS)/SETUP.EXE: $(DOS_SETUP)
	@mkdir -p $(DOS)
	cp $< $@

# The Watcom artifacts are built out-of-band (podman toolchain); if they are
# missing, fail with the command to produce them rather than a cryptic make error.
$(ROOT)/out/adp32.exe:
	@echo "DOS player not built. Run: dos-toolchain wmake -h -f Makefile-dos MODE=32"
	@exit 1

$(ROOT)/out/setup.exe:
	@echo "DOS SETUP not built. Run: dos-toolchain wmake -h -f Makefile-dos MODE=16"
	@exit 1

$(DOS)/README.TXT:
	@mkdir -p $(DOS)
	@printf 'La Aventura Original - DOS (386+)\r\n\r\n' > $@
	@printf 'Run SETUP.EXE once to choose the graphics mode and configure sound,\r\n' >> $@
	@printf 'then run ADP.EXE to play.\r\n\r\n' >> $@
	@printf '  VGA   320x200, 256 colours (Mode-X)\r\n' >> $@
	@printf '  SVGA  640x400, 256 colours (VESA)\r\n' >> $@

# ----------------------------------------------------------------------- web
# Stages the SDL data set into web-games/ and delegates to the top-level
# emscripten makefile, which bakes it into the preload bundle. The output
# directory is removed first because the preload data is not a tracked
# prerequisite of the top-level link rule.

web: $(SVGA_ALL)
	@command -v emcc >/dev/null 2>&1 || \
		{ echo "emcc not found: install emsdk and 'source emsdk_env.sh' first"; exit 1; }
	rm -rf $(WEBSTAGE) $(WEBOUT)
	mkdir -p $(WEBSTAGE)
	cp $(SVGA_ALL) $(WEBSTAGE)/
	$(MAKE) -C $(ROOT) -f Makefile-web DEBUG=$(WEB_DEBUG) out/web/original256/index.html
	@mkdir -p $(WEB)
	cp $(WEBOUT)/* $(WEB)/

# --------------------------------------------------------------- maintenance

tools:
	$(MAKE) -C $(ROOT) -f $(HOST_MK) out/adpc out/dmg out/dsk

$(ADPC):
	$(MAKE) -C $(ROOT) -f $(HOST_MK) out/adpc

$(DMG):
	$(MAKE) -C $(ROOT) -f $(HOST_MK) out/dmg

$(DSK):
	$(MAKE) -C $(ROOT) -f $(HOST_MK) out/dsk

inspect:
	$(PYTHON) scripts/iff-to-png.py part1-ham -o inspection/part1
	$(PYTHON) scripts/iff-to-png.py part2-ham -o inspection/part2

ham:
	$(PYTHON) scripts/build-ham.py

ham-check:
	$(PYTHON) scripts/build-ham.py --check

clean:
	rm -rf $(RELEASE) inspection $(WEBSTAGE) $(WEBOUT)
