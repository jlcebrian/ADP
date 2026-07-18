@echo off
setlocal
pushd "%~dp0"
if defined PYTHON_EXE goto :run_python_exe
where py >nul 2>nul && goto :run_py
where python >nul 2>nul && goto :run_python
echo Python 3 was not found. Set PYTHON_EXE to the Python executable.
popd
exit /b 1

:run_python_exe
"%PYTHON_EXE%" scripts\iff-to-png.py part1-ham -o inspection\part1
if errorlevel 1 goto :failed
"%PYTHON_EXE%" scripts\iff-to-png.py part2-ham -o inspection\part2
goto :finished
:run_py
py -3 scripts\iff-to-png.py part1-ham -o inspection\part1
if errorlevel 1 goto :failed
py -3 scripts\iff-to-png.py part2-ham -o inspection\part2
goto :finished
:run_python
python scripts\iff-to-png.py part1-ham -o inspection\part1
if errorlevel 1 goto :failed
python scripts\iff-to-png.py part2-ham -o inspection\part2
:finished
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
:failed
popd
exit /b 1
