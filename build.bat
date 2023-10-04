@echo off
SETLOCAL
SET VERSION=BETA0.2

IF "%1"=="Windows" GOTO :WINDOWS
IF "%1"=="Amiga" GOTO :AMIGA
IF "%1"=="DOS" GOTO :DOS
IF "%1"=="Web" GOTO :WEB
IF "%2"=="" GOTO :ERROR
%2
GOTO :EOF

:ERROR
echo Usage: build.bat [Windows] [Release]
GOTO :EOF

REM -----------------------------------------------------------------
REM  Emscripten web player
REM -----------------------------------------------------------------
:WEB
SETLOCAL
CALL EMSDK_ENV.BAT >NUL 2>&1
SET OPTS=
IF "%2"=="clean" SET OPTS=clean
emmake gnumake -f Makefile-web %OPTS%
GOTO :EOF

REM -----------------------------------------------------------------
REM  Amiga player
REM -----------------------------------------------------------------
:AMIGA
SETLOCAL
SET VSCODE=c:\Users\%USERNAME%\.vscode
SET EXTENSION=extensions\bartmanabyss.amiga-debug-1.7.4
SET OPTS=
IF "%2"=="clean" SET OPTS=clean
SET PATH=%VSCODE%\%EXTENSION%\bin\win32;%VSCODE%\%EXTENSION%\bin\win32\opt\bin;%PATH%
gnumake -f Makefile-amiga %OPTS%
GOTO :EOF

REM -----------------------------------------------------------------
REM  MS-DOS player (Open Watcom 2.0)
REM -----------------------------------------------------------------
:DOS
SETLOCAL
CALL OWSETENV.BAT
wmake -h -f Makefile-dos %2
GOTO :EOF

REM -----------------------------------------------------------------
REM  Windows x64 executables compiled with Visual Studio
REM -----------------------------------------------------------------
:WINDOWS
WHERE cl >NUL 2>NUL
IF %ERRORLEVEL% NEQ 0 call devenv.bat

SETLOCAL

SET VERSION=/DVERSION=%VERSION%
SET TRACE=/DTRACE_ON=1
SET WARNINGS=/W4 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE 
SET TOOLOPTS=/Iinclude-tools /Ilib-win64/libpng
SET OPTS=/Fo:obj\ /Fd:out\ /nologo /MT /Ilib-win64\libpng /Ilib-win64\sdl\include /Iinclude /D_STDCLIB /DNO_CACHE /DHAS_CLIPBOARD /DHAS_FULLSCREEN /DHAS_VIRTUALFILESYSTEM
set OPTIM=/Zi /GR- /EHsc /MP8 /FC /GL
SET LINK=

IF "%2"=="Release" SET OPTIM=/O2 /GR- /EHsc /MP8 /GL
IF "%2"=="Release" SET LINK=/DEBUG:NONE

SET LIBS=lib-win64\sdl2\lib\SDL2-static.lib lib-win64\sdl2\lib\SDL2main.lib user32.lib gdi32.lib comdlg32.lib ole32.lib shell32.lib setupapi.lib winmm.lib imm32.lib version.lib advapi32.lib oleaut32.lib
SET TOOLLIBS=lib-win64\libpng\libpng16.lib lib-win64\zlib\zlib.lib

echo ---- Compiling DC
cl %VERSION% %OPTS% %TOOLOPTS% %OPTIM% /Fe:out\dc.exe ^
	src-common\os_file.cpp ^
	src-common\os_lib.cpp ^
	src-common\os_mem.cpp ^
	src-common\os_arena.cpp ^
	src-windows\files.cpp ^
	src-common\dim.cpp ^
	src-common\dim_adf.cpp ^
	src-common\dim_cpc.cpp ^
	src-common\dim_fat.cpp ^
	src-tools\dc_main.cpp ^
	src-tools\dc_cp437.cpp ^
	src-tools\dc_cp850.cpp ^
	src-tools\dc_cp1252.cpp ^
	src-tools\dc_char.cpp ^
	src-tools\dc_symb.cpp ^
	src-tools\tool_dc.cpp ^
	%TOOLLIBS% /link %LINK% /SUBSYSTEM:CONSOLE
IF %ERRORLEVEL% NEQ 0 GOTO :EOF

echo ---- Compiling DSK
cl %VERSION% %OPTS% %TOOLOPTS% %OPTIM% /Fe:out\dsk.exe ^
	src-common\os_file.cpp ^
	src-common\os_lib.cpp ^
	src-common\os_mem.cpp ^
	src-windows\files.cpp ^
	src-common\dim.cpp ^
	src-common\dim_adf.cpp ^
	src-common\dim_cpc.cpp ^
	src-common\dim_fat.cpp ^
	src-tools\tool_dsk.cpp ^
	%TOOLLIBS% /link %LINK% /SUBSYSTEM:CONSOLE
IF %ERRORLEVEL% NEQ 0 GOTO :EOF

echo ---- Compiling CHR
cl %VERSION% %OPTS% %TOOLOPTS% %OPTIM% /Fe:out\chr.exe ^
	src-common\os_file.cpp ^
	src-common\os_lib.cpp ^
	src-common\os_mem.cpp ^
	src-windows\files.cpp ^
	src-common\dim.cpp ^
	src-common\dim_adf.cpp ^
	src-common\dim_cpc.cpp ^
	src-common\dim_fat.cpp ^
	src-tools\img.cpp ^
	src-tools\tool_chr.cpp ^
	%TOOLLIBS% /link %LINK% /SUBSYSTEM:CONSOLE
IF %ERRORLEVEL% NEQ 0 GOTO :EOF

echo ---- Compiling DMG
cl %VERSION% %OPTS% %TOOLOPTS% %OPTIM% /Fe:out\dmg.exe ^
	src-common\dmg.cpp ^
	src-common\dmg_imgc.cpp ^
	src-common\dmg_imgp.cpp ^
	src-common\dmg_rlec.cpp ^
	src-common\dmg_univ.cpp ^
	src-common\dmg_cga.cpp ^
	src-common\dmg_ega.cpp ^
	src-common\ddb_pal.cpp ^
	src-common\os_file.cpp ^
	src-common\os_lib.cpp ^
	src-common\os_mem.cpp ^
	src-windows\files.cpp ^
	src-common\dim.cpp ^
	src-common\dim_adf.cpp ^
	src-common\dim_cpc.cpp ^
	src-common\dim_fat.cpp ^
	src-tools\img.cpp ^
	src-tools\tool_dmg.cpp ^
	src-tools\dmg_edit.cpp ^
	%TOOLLIBS% /link %LINK% /SUBSYSTEM:CONSOLE
IF %ERRORLEVEL% NEQ 0 GOTO :EOF

echo ---- Compiling PLAYER
cl %VERSION% %OPTS% %OPTIM% /Fe:out\player.exe  /DDEBUG_ALLOCS ^
	src-common\ddb.cpp ^
	src-common\os_char.cpp ^
	src-common\ddb_data.cpp ^
	src-common\ddb_dump.cpp ^
	src-common\ddb_inp.cpp ^
	src-common\ddb_pal.cpp ^
	src-common\ddb_play.cpp ^
	src-common\ddb_run.cpp ^
	src-common\ddb_vid.cpp ^
	src-common\ddb_scr.cpp ^
	src-common\dmg_cach.cpp ^
	src-common\dmg_imgc.cpp ^
	src-common\dmg_imgp.cpp ^
	src-common\dmg_rlec.cpp ^
	src-common\dmg_univ.cpp ^
	src-common\dmg_cga.cpp ^
	src-common\dmg_ega.cpp ^
	src-common\dmg.cpp ^
	src-common\dim.cpp ^
	src-common\dim_adf.cpp ^
	src-common\dim_cpc.cpp ^
	src-common\dim_fat.cpp ^
	src-common\os_file.cpp ^
	src-common\os_lib.cpp ^
	src-common\os_mem.cpp ^
	src-common\scrfile.cpp ^
	src-sdl\player.cpp ^
	src-sdl\video.cpp ^
	src-windows\error.cpp ^
	src-windows\files.cpp ^
	%LIBS% lib-win64\ddb.res /link %LINK% /SUBSYSTEM:WINDOWS
IF %ERRORLEVEL% NEQ 0 GOTO :EOF

echo ---- Compiling DDB
cl %VERSION% %OPTS% %OPTIM% %TRACE% /Fe:out\ddb.exe ^
	src-common\ddb.cpp ^
	src-common\os_char.cpp ^
	src-common\ddb_data.cpp ^
	src-common\ddb_dump.cpp ^
	src-common\ddb_inp.cpp ^
	src-common\ddb_pal.cpp ^
	src-common\ddb_play.cpp ^
	src-common\ddb_run.cpp ^
	src-common\ddb_vid.cpp ^
	src-common\ddb_scr.cpp ^
	src-common\dmg_cach.cpp ^
	src-common\dmg_imgc.cpp ^
	src-common\dmg_imgp.cpp ^
	src-common\dmg_rlec.cpp ^
	src-common\dmg_univ.cpp ^
	src-common\dmg_cga.cpp ^
	src-common\dmg_ega.cpp ^
	src-common\dmg.cpp ^
	src-common\dim.cpp ^
	src-common\dim_adf.cpp ^
	src-common\dim_cpc.cpp ^
	src-common\dim_fat.cpp ^
	src-common\os_file.cpp ^
	src-common\os_lib.cpp ^
	src-common\os_mem.cpp ^
	src-common\scrfile.cpp ^
	src-tools\tool_ddb.cpp ^
	src-sdl\video.cpp ^
	src-windows\error.cpp ^
	src-windows\files.cpp ^
	%LIBS% lib-win64\ddb.res /link %LINK% /SUBSYSTEM:CONSOLE
IF %ERRORLEVEL% NEQ 0 GOTO :EOF

:EOF
REM del *.obj