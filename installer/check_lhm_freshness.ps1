<#
.SYNOPSIS
    Fails if installer/LibreHardwareMonitor/lhm-temp.exe is missing or
    stale relative to its source or the LHM library it's built against.

.DESCRIPTION
    installer/LibreHardwareMonitor/lhm-temp.exe is a gitignored build
    artifact (see PORT_STATUS.md's Known Issues) that setup.iss bundles
    into the installer as-is, with no freshness check of its own. A stale
    local compile sitting in that folder silently produced no GPU sensor
    data with zero visible errors anywhere - found and fixed 2026-09-15.

    Run this right before building the installer (ISCC.exe setup.iss) as a
    pre-flight assertion. It does not rebuild anything - see
    build_lhm_temp.ps1 for that; run this after it, or after any manual
    build, to confirm the result is actually fresh before packaging it.

.EXAMPLE
    installer\check_lhm_freshness.ps1
    # exit 0 and prints OK if fresh, exit 1 and explains why otherwise
#>
param()

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $root "LibreHardwareMonitor\lhm-temp.exe"
$csPath = Join-Path $root "lhm-temp\lhm-temp.cs"
$dllPath = Join-Path $root "LibreHardwareMonitor\LibreHardwareMonitorLib.dll"

function Fail($message) {
    Write-Host "FAIL: $message" -ForegroundColor Red
    Write-Host "Run installer\build_lhm_temp.ps1 to fix this." -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path $exePath)) {
    Fail "$exePath does not exist."
}

$exeTime = (Get-Item $exePath).LastWriteTimeUtc

if (Test-Path $csPath) {
    $csTime = (Get-Item $csPath).LastWriteTimeUtc
    if ($exeTime -lt $csTime) {
        Fail "lhm-temp.exe ($exeTime) is older than lhm-temp.cs ($csTime) - it wasn't rebuilt after the source changed."
    }
} else {
    Write-Host "WARNING: $csPath not found, skipping source-freshness check." -ForegroundColor Yellow
}

if (Test-Path $dllPath) {
    $dllTime = (Get-Item $dllPath).LastWriteTimeUtc
    if ($exeTime -lt $dllTime) {
        Fail "lhm-temp.exe ($exeTime) is older than LibreHardwareMonitorLib.dll ($dllTime) - it wasn't rebuilt after the LHM library was (re)downloaded/updated."
    }
} else {
    Write-Host "WARNING: $dllPath not found, skipping library-freshness check." -ForegroundColor Yellow
}

Write-Host "OK: lhm-temp.exe is at least as fresh as its source and the LHM library." -ForegroundColor Green
exit 0
