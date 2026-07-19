@echo off
setlocal
pushd "%~dp0"

set "ROOT=%~dp0..\.."
set "ADPC=%ROOT%\out\win64\adpc.exe"
set "DMG=%ROOT%\out\win64\dmg.exe"
set "DSK=%ROOT%\out\win64\dsk.exe"
set "TR=--tr U+00DA=35 --tr U+00C1=36 --tr U+00D3=24 --tr U+00CD=37 --tr U+00C9=38"
set "WIN=release\windows"
set "AMIGA=release\amiga-ham"

if not exist "%ADPC%" goto :missing_adpc
if not exist "%DMG%" goto :missing_dmg
if not exist "%DSK%" goto :missing_dsk
if not exist "%WIN%" mkdir "%WIN%"
if not exist "%AMIGA%" mkdir "%AMIGA%"

"%ADPC%" --target ibmpc %TR% part1.sce "%WIN%\part1.ddb" || goto :failed
"%ADPC%" --target ibmpc %TR% part2.sce "%WIN%\part2.ddb" || goto :failed
"%ADPC%" --target amiga %TR% part1.sce "%AMIGA%\part1.ddb" || goto :failed
"%ADPC%" --target amiga %TR% part2.sce "%AMIGA%\part2.ddb" || goto :failed

"%DMG%" create "%WIN%\part1.dat" part1-svga -format=dat5 -mode=planar8 -width=480 -height=192 -remap=16-255 -screen=640x400 -2x=1 || goto :failed
"%DMG%" create "%WIN%\part2.dat" part2-svga -format=dat5 -mode=planar8 -remap=16-255 -screen=640x400 -2x=1 || goto :failed
"%DMG%" create "%AMIGA%\part1.dat" part1-ham -format=dat5 -mode=ham6 -width=240 -height=96 -screen=320x200 || goto :failed
"%DMG%" create "%AMIGA%\part2.dat" part2-ham -format=dat5 -mode=ham6 -screen=320x200 || goto :failed

"%DMG%" test "%WIN%\part1.dat" >nul || goto :failed
"%DMG%" test "%WIN%\part2.dat" >nul || goto :failed
"%DMG%" test "%AMIGA%\part1.dat" >nul || goto :failed
"%DMG%" test "%AMIGA%\part2.dat" >nul || goto :failed

copy /y part1.chr "%WIN%\part1.chr" >nul
copy /y part2.chr "%WIN%\part2.chr" >nul
copy /y part1-svga\part1.scr "%WIN%\part1.scr" >nul
copy /y windows.cfg "%WIN%\part1.cfg" >nul
copy /y "%ROOT%\daad-ready\dist\ASSETS\WINDOWS_EXPERIMENTAL\adp-player.exe" "%WIN%\ad.exe" >nul
copy /y part1.chr "%AMIGA%\part1.chr" >nul
copy /y part2.chr "%AMIGA%\part2.chr" >nul
copy /y part1-amiga-ham.iff "%AMIGA%\part1.scr" >nul
copy /y amiga.cfg "%AMIGA%\part1.cfg" >nul
>>"%AMIGA%\part1.cfg" echo FILES=part1.ddb,part1.dat,part1.chr,part1.scr,part1.cfg,part2.ddb,part2.dat,part2.chr
copy /y "%ROOT%\daad-ready\dist\ASSETS\AMIGA_EXPERIMENTAL\ADP.EXE" "%AMIGA%\ADP.EXE" >nul
if not exist "%AMIGA%\s" mkdir "%AMIGA%\s"
>"%AMIGA%\s\startup-sequence" echo ADP.EXE
"%DSK%" create -b -r "release\original256-amiga-ham.adf" hd "%AMIGA%" || goto :failed

echo Distributables written to %~dp0release
popd
exit /b 0

:missing_adpc
echo ADPC not found: %ADPC%
goto :failed
:missing_dmg
echo DMG not found: %DMG%
goto :failed
:missing_dsk
echo DSK not found: %DSK%
goto :failed
:failed
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" set "RESULT=1"
popd
exit /b %RESULT%
