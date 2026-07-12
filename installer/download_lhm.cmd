@echo off
REM Download and extract LibreHardwareMonitor portable for Conky installer
REM Requires PowerShell 5+
setlocal

set "LHM_DIR=%~dp0LibreHardwareMonitor"
set "TEMP_ZIP=%TEMP%\LibreHardwareMonitor.zip"

echo === Downloading LibreHardwareMonitor ===
powershell -Command "& {
    $url = 'https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip'
    Write-Output 'Downloading from: ' + $url
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile '%TEMP_ZIP%'
    if ($LASTEXITCODE -ne 0) { exit 1 }
}"

if %ERRORLEVEL% neq 0 (
    echo Download failed. Trying alternative URL...
    powershell -Command "& {
        $url = 'https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip'
        Write-Output 'Downloading from: ' + $url
        Invoke-WebRequest -Uri $url -OutFile '%TEMP_ZIP%'
    }"
    if %ERRORLEVEL% neq 0 (
        echo Download failed. Get it manually from:
        echo   https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases
        pause
        exit /b 1
    )
)

echo === Extracting to %LHM_DIR% ===
powershell -Command "& {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory('%TEMP_ZIP%', '%LHM_DIR%')
    Write-Output 'Extracted successfully'
}"

REM Clean up
del "%TEMP_ZIP%" 2>nul

REM List what we got
echo === Files extracted ===
dir /b "%LHM_DIR%" | findstr /i "exe dll"

echo.
echo === Done! LibreHardwareMonitor files ready for installer ===
