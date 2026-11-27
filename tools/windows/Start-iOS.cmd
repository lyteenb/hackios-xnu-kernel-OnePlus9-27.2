@echo off
rem SPDX-License-Identifier: MIT
setlocal EnableExtensions DisableDelayedExpansion
set "interactive="
set "action=%~1"
if not defined action (
    set "action=start"
    set "interactive=1"
)

for %%A in (start verify status android) do if /I "%action%"=="%%A" (
    set "action=%%A"
    goto prepare
)
echo Usage: %~nx0 [start^|verify^|status^|android]
exit /b 2

:prepare
set "result=1"
pushd "%~dp0" || goto finish
if not exist "launch.py" goto missing_package
if not exist "manifest.json" goto missing_package

rem Use workspace tools when this package is under out\releases.
set "python=python.exe"
for %%D in ("%~dp0..\..\..") do set "workspace=%%~fD"
if exist "%workspace%\.venv\Scripts\python.exe" set "python=%workspace%\.venv\Scripts\python.exe"
set "local_tools=%workspace%\.local-tools\platform-tools-parent\platform-tools"
if exist "%local_tools%\adb.exe" if exist "%local_tools%\fastboot.exe" set "PATH=%local_tools%;%PATH%"
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

"%python%" -c "import sys; sys.exit(sys.version_info < (3, 11))" >nul 2>&1
if errorlevel 1 goto missing_python
if /I "%action%"=="verify" goto run
where adb.exe >nul 2>&1 || goto missing_usb
where fastboot.exe >nul 2>&1 || goto missing_usb

:run
"%python%" "%~dp0launch.py" %action%
set "result=%errorlevel%"
goto cleanup

:missing_package
echo Keep %~nx0 beside launch.py and manifest.json in the private RAM boot package.
goto cleanup

:missing_python
echo Python 3.11 or newer is required on PATH or in the workspace virtual environment.
goto cleanup

:missing_usb
echo Add Android platform-tools ^(adb and fastboot^) to PATH for this session.

:cleanup
popd

:finish
if "%result%"=="0" (
    echo.
    echo Completed: %action%
) else (
    echo.
    echo %action% failed ^(exit %result%^).
)
if defined interactive pause
exit /b %result%
