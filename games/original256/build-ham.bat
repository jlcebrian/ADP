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
"%PYTHON_EXE%" scripts\build-ham.py %*
goto :finished
:run_py
py -3 scripts\build-ham.py %*
goto :finished
:run_python
python scripts\build-ham.py %*
:finished
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
