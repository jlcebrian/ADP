# What's new?

## ADP 0.2

### All versions

* Fixed a number of issues related to the loading screen, especially for single part games. Those games should now show its loading screen and wait for a keypress to continue.
* Fixed an issue where INK/PAPER condacts would change the colors of text printed before them.
* Fixed ANYKEY not printing sysmsg 16 as it should.
* Fixed continuous LISTOBJ looking for the wrong flag bit.

### Desktop & web versions

* **NEW:** Added clipboard support (Ctrl+C/Ctrl+V). Since character selection is not supported (yet), Ctrl+C copies the entire current line to the clipboard.
* **NEW:** The command line DDB utility, and the web player, now support loading disk images in Amiga, Atari ST and MS-DOS format.
* **NEW:** The web player version now supports playing games from non-embedded data, so it can be used to package and release new games to the web. See the web version package for details.

### Amiga

* Fixed image support for version 1 games (Original, Jabato)
* Fixed a number of issues which were crashing the game on exit in Amiga 1200+
* **NEW:** This version now supports digital sample playback. This also includes the keypress click, press F10 to toggle it.

### Atari ST

* **NEW:** This version also supports digital sample playback now. This uses the AY chip, just like the original interpreters, so the quality is not great but is should work in any ST computer. The keypress click is *not* included, because ST DAAD uses the system click sound. 

## ADP 0.1

First public release of ADP, with 16-bits DDB file support.