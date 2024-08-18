# ADP: ADventure Player

<p align="center">
    <img src="/docs/Logo.png" width="414.5" height="95.5" />
</p>
<p align="center">
https://www.adventure-player.com
</p>

(Note: there is a [Spanish version](LEEME.md) of this file!)

**DAAD** was an authoring tool designed to write text adventure
games targetting a number of early 90's home computers. It was 
developed by Tim Gilberts for the Spanish company **Aventuras AD**.

**ADP** is a portable interpreter written in minimal C++. It can run 
adventures made in **DAAD** in modern platforms, with special
care to support all the features present in games by **Aventuras AD**,
including palette manipulation, double buffer, and digital sound.
It currently runs the 16-bits versions of the adventures (Amiga,
Atari ST and PC) with 8-bit version support planned for later.

In addition to modern platforms, the ADP interpreter has been
ported natively to Amiga, Atari ST, and MS-DOS, so it can be
used as a direct replacement for the original interpreters.
There is also Emscripten support to embed games in a web page.

### Disclaimer

This is early software in a *BETA* stage. While the program seems to
be reasonably feature-complete, bugs are expected and some games
may still not work properly. Bug reports are welcome!

## How to use ADP

**ADP** releases contain two executable files: a command line utility
called DDB, and a game player called PLAYER. Retro ports of the
interpreter will include only the PLAYER utility.

The DDB command line utility requires as a parameter the name of
a .DDB file, a game database built by a **DAAD** compiler with an
Amiga, Atari ST or IBM PC target. In addition to the DDB file,
the interpreter supports the presence of other files which should
be present in the same folder and with the same name as the DDB:

```
		.DDB		Adventure game database
		.DAT		Graphics database (it can also be .EGA or .CGA)
		.CHR		Character set (it can also be .CH0 for ST)
		.SCR		Loading/splash screen (it can also be .EGS, .CGS or .VGS)
```

PLAYER simply will run any DDB file it finds in the same folder as
itself. If multiple DDB files are present in that folder, it will
request for a 'part #' to the user.

When running a game which includes EGA/CGA versions, the specific
version to run can be specified running PLAYER EGA or PLAYER CGA.

## Notes for specific versions

### Windows / Mac

You may get a false positive warning from Windows Defender or
your antivirus of choice. Those programs tend to flag as
dangerous *any* new EXE file they don't know about, with the
warning going away eventurally as more people run it and it
is whitelisted in their databases.

### MS-DOS

This version requires a 386 processor or better, 2MB of RAM,
and an VGA card. Although EGA/CGA games are playable, they
will always run in VGA mode.

A SoundBlaster card is supported and, if present, will be
user to play digital samples in games which feature them.

### Amiga / Atari ST

Both versions will run in base computers with 512K of RAM,
but any extra RAM found will be used as a cache to speed
up the loading of images from disk.

As the time of writing this file, both versions are still
missing digital sample playback support and multiple disk
distributions (where a multiple part game includes all its
DDB files in the first disk, but the DAT graphic databases
are in another disk, making the loader prompt when needed).

## Compatibility

Currently, **ADP** only supports DDB databases compiled for Amiga,
Atari ST, or IBM PC. Support for additional platforms will be
coming in a future release.

At the moment, English databases are completely untested.
They should work fine, but there may be issues with parsing.
Bug reports are always welcome.

**ADP** supports both versions 1 and 2 of **DAAD**. Keep in mind that
no version 1 of the compiler survives, so compatibility will be
reduced to the existing commercial games (Aventura Original, Jabato).

All **ADP** versions support all target platforms. That means you
can play PC/EGA versions of games in Atari ST, for example. However,
performance won't be great, since an optimized image decompressor
has been provided only for native version 2 .DAT files. This can also
introduce a few caveats. For example, in some games we've found Amiga 
versions using DDB files marked for Atari ST. This could produce a 
corrupted splash screen, although the player will try to guess the 
correct environment in those cases.

At the moment, disk image files (.ADF, .DSK) are not supported, so
if you have any Amiga/ST/PC games in that format, you'll have to
extract the game files first, using an external tool. Direct image
support is planned for a future release.

## Changes and new features

**ADP** does not extend the original **DAAD** with any additional
libraries, condacts or capabilities. However, a few features that
were not present in the original interpreters have
been implemented for convenience:

### Command line editor

The editor now has a history of commands. You can press the up/down
cursor keys to navigate previously entered commands.

You can search the history database using F8. For example, writing
'do' and pressing F8 will autocomplete the line with the most
recent command entered which started with the letters 'do'.
Pressing F8 again searchs the next entry.

There is also UNDO support in the editor. Pressing Ctrl+Z and
Ctrl+Y will undo and redo the last change, respectively.

The editor supports Ctrl+Left and Ctrl+Right to move the cursor
word by word, Ctrl+Backspace/Delete to delete entire words,
and the ESC key to clear the entire line.

### Keypress sound

The Atari ST version has keypress 'click' sound by default, so
this has been implemented for any **ADP** versions which
supports digital sample playback. Two samples are provided. 
Press F10 to toggle the key sound or turn it off.

### SAVE/LOAD

In platforms which support it, SAVE and LOAD will offer a
standard system save/load file dialog.

### WHATO

When WHATO does not produce a suitable object, but there is
an adjective in the user provided phrase (but no noun, and
no unknown words), WHATO will try to resolve the phrase
identifying the required object just with the adjective.
This is an experimental change which makes Templos Sagrados
more playable, and it may disappear in a future release.

### PICTURE

The original PICTURE command loads from disk (or cache) and 
decompresses an image to an internal buffer. Afterwards, 
DISPLAY is used to dump the buffer into the screen at high 
speed. This scheme provides a way for games to make simple
animations by displaying the same image in multiple positions
and clipping windows.

**ADP** supports multiple pictures in the internal buffer,
so you can potentially produce animations with multiple
pictures. PICTURE will return immediatelly if the intended
picture is already in RAM. The size of the internal buffer
varies, but it is guaranteed to be big enough for half
a screen of pictures.

### Forced delays

**ADP** will introduce artifical delays and pauses in some
cases, in order to reproduce animations from the original
Aventuras AD games which expect a slow computer and do not
have PAUSE commands to delay every animation frame.

In essence, drawing a picture which overrides a screen
area that has already been modified this frame, changing
a palette color that has already been changed, or invoking
a buffer change, will introduce a small delay.

In addition, text windows will scroll with a delay after
the [More...] prompt (this change is experimental, and
is provided for readability in cases like Cozumel's
intro text, where an instant scroll provides no clue
about where the user needs to continue reading).

## How to build ADP from the source code

Due to the exotic platforms supported, currently **ADP** relies on simple
GNU Make files to build, so it may requires some manual work on 
your part in order to install the required dependencies.

Note that the compilation will produce a number of command line tools 
that are not part of the **ADP** distribution (yet). Those tools are
a work in progress and are not yet ready to use.

### Windows

This build requires Visual Studio 2022 or later. Enter a x64 native
command prompt (using the start menu icon or calling vcvars64.bat)
and run the following command:

```
C> build Windows Release
```

For convenience, this repository includes x64 LIB files for libpng,
zlib, and SDL. A static version of the SDL library is linked, in
order to create a portable EXE file with no dependencies.

### Unix-like version (Linux and OSX)

This version requires a recent version of SDL2 and libpng, with
working libpng-config and sdl2-config command line tools, alongside
a C++ compiler of your choice, and GNU Make.

For OSX, I'd recommend installing the required dependencies using
Homebrew, alongside the Xcode command line tools.

Once the requirements are meet, simply compily using Makefile-unix:

```
$ make -f Makefile-unix
```

### Emscripten (web port)

This version requires a recent version of emscripten with support for
SDL2 programs (this should be installed automatically during the
compilation process, but if you are using your Linux distribution's
provided emscripten, your mileage may vary).

Currently, the compiled web port embeds the game files, which means
a build must be made for every game you want to port to the web.
To create your web players, add a folder for every game to the
'web-games' directory and put all the game's files (.DDB, .DAT, .CHR)
inside. The Makefile will produce a web page for every game inside 
the out/web directory.

This port currently uses [KioskBoard](https://github.com/furcan/KioskBoard)
for virtual keyboard support on mobile.

```
$ emmake gnumake -f Makefile-web
```

If you are in Windows, make sure EMSDK_ENV.bat is in your path and call:

```
C> build Web
```

### MS-DOS

This version requires a recent version of 
[Open Watcom 2](https://github.com/open-watcom/open-watcom-v2#open-watcom-v2-fork), 
a fork of the legendary Watcom C/C++ compiler with support for a
more modern C++ language and standard libraries (and many fixes).

Run OWSETENV.bat and build with:

```
C> wmake -h -f Makefile-dos
```

If you are in Windows and OWSETENV.bat is in your path, you can also run:

```
C> build DOS
```

### Amiga

Currently, this version uses the [Visual Studio Code toolchain](https://github.com/BartmanAbyss/vscode-amiga-debug)
by Bartman^Abyss. Enter a command line environment and add the extension's
platform folder to your PATH, in addition to the opt/bin subfolder.

```
# OSX
$ PATH=~/.vscode/extensions/bartmanabyss.amiga-debug-1.7.2/bin/darwin;$PATH
$ PATH=~/.vscode/extensions/bartmanabyss.amiga-debug-1.7.2/bin/darwin/opt/bin;%PATH%
$ make -f Makefile-amiga
```

```
# Linux
$ PATH=~/.vscode/extensions/bartmanabyss.amiga-debug-1.7.2/bin/linux;$PATH
$ PATH=~/.vscode/extensions/bartmanabyss.amiga-debug-1.7.2/bin/linux/opt/bin;%PATH%
$ make -f Makefile-amiga
```

```
# Windows
$ PATH=~/.vscode/extensions/bartmanabyss.amiga-debug-1.7.2/bin/win32;$PATH
$ PATH=~/.vscode/extensions/bartmanabyss.amiga-debug-1.7.2/bin/win32/opt/bin;%PATH%
$ gnumake -f Makefile-amiga
```

If you are in Windows, you can also run `build Amiga`, but you'll probably
have to manually modify the build.bat file in order to fix the right
path for the toolchain.

### Atari ST

This version uses the m68k-atari-mint cross-compilation tools provided by
Thorsten Otto at https://tho-otto.de/crossmint.php.

You'll need to install at least the following packages:

* binutils
* fdlibm
* gcc (a modern version, such as 13+)
* gemlib
* mintlib

You'll also need a working VASM assembler (http://sun.hasenbraten.de/vasm/),
and libcmini 0.54. The included Makefile assumes LIBCMINI to be installed
in $HOME/libcmini, but you can set a LIBCMINI environment variable to the
proper path if that's not the case. Note that ADP is C++ code, and libcmini
may expect their headers to be included into pure C code only, so if you
find linker errors you can either recompile libcmini to have the proper
symbols, or hack a few extern "C" {} in its headers. The VASM executable
is expected to be named vasmm68k_mot (make sure it's in your PATH).

Once all requirements are met, compile with

```
$ make -f Makefile-atarist
```


## License

**ADP** has been release under the terms of the MIT license.
See the included LICENSE file for details.