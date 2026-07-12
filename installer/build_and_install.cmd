@echo off
REM Build Conky Windows port and install to Program Files
REM Run from PowerShell/Terminal in the repo root.

setlocal

set "BUILD_DIR=%~dp0build"
set "INSTALLER_DIR=%~dp0installer"
set "EXE_SRC=%BUILD_DIR%\src\conky.exe"
set "EXE_DST=%ProgramFiles%\Conky\conky.exe"

REM Step 1: Build
echo === Building Conky ===
cd /d "%BUILD_DIR%"
mingw32-make.exe -j4 conky.exe
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b 1
)

REM Step 2: Kill existing
taskkill /F /IM conky.exe 2>nul

REM Step 3: Copy to Program Files
echo === Installing to %ProgramFiles%\Conky ===
if not exist "%ProgramFiles%\Conky" mkdir "%ProgramFiles%\Conky"
copy /Y "%EXE_SRC%" "%EXE_DST%"
copy /Y "%~dp0installer\run_conky.cmd" "%ProgramFiles%\Conky\run_conky.cmd"

REM Step 4: Create startup shortcut (optional)
echo === To start at login, add a shortcut to run_conky.cmd in shell:startup ===

echo.
echo === Done! Run from Start Menu or: "%ProgramFiles%\Conky\run_conky.cmd" ===
echo.
