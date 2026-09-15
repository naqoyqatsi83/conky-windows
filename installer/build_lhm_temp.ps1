<#
.SYNOPSIS
    Compiles installer/lhm-temp/lhm-temp.cs into
    installer/LibreHardwareMonitor/lhm-temp.exe.

.DESCRIPTION
    This is the single, authoritative way to (re)build lhm-temp.exe - always
    run this instead of invoking csc.exe by hand. It exists specifically
    because a stale, manually-built lhm-temp.exe sitting in the working tree
    (it's a gitignored build artifact - see PORT_STATUS.md's Known Issues)
    caused a real bug 2026-09-15: setup.iss bundles whatever .exe happens to
    be in that folder without checking freshness, and a stale local compile
    silently failed to produce GPU sensor data with zero visible errors
    anywhere. Always running this script instead of a remembered manual
    command makes "forgot to recompile" structurally impossible rather than
    relying on discipline.

    Requires LibreHardwareMonitor already downloaded to
    installer\LibreHardwareMonitor (run download_lhm.cmd first if needed).

.EXAMPLE
    installer\build_lhm_temp.ps1
#>
param()

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$csFile = Join-Path $root "lhm-temp\lhm-temp.cs"
$dll = Join-Path $root "LibreHardwareMonitor\LibreHardwareMonitorLib.dll"
$outExe = Join-Path $root "LibreHardwareMonitor\lhm-temp.exe"

if (-not (Test-Path $dll)) {
    Write-Error "LibreHardwareMonitorLib.dll not found at $dll - run download_lhm.cmd first."
    exit 1
}
if (-not (Test-Path $csFile)) {
    Write-Error "lhm-temp.cs not found at $csFile"
    exit 1
}

# Multiple .NET Framework versions can be installed side by side (this repo
# needs 4.x for System.Linq, used by lhm-temp.cs). -First 1 on an
# unsorted directory listing found the ancient v2.0.50727 csc.exe here on a
# dev machine that also has v4.0.30319 -- sort by folder name descending so
# the newest wins deterministically instead of directory-enumeration order.
$csc = Get-ChildItem "$env:windir\Microsoft.NET\Framework64" -Recurse -Filter "csc.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $csc) {
    Write-Error "csc.exe not found under $env:windir\Microsoft.NET\Framework64 (.NET Framework 4.x required)"
    exit 1
}

Write-Host "Compiler: $($csc.FullName)"
Write-Host "Source:   $csFile"
Write-Host "Output:   $outExe"

& $csc.FullName -nologo -target:winexe `
    -reference:"$dll" `
    -out:"$outExe" `
    "$csFile"

if ($LASTEXITCODE -ne 0) {
    Write-Error "csc.exe failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "Built $outExe" -ForegroundColor Green
