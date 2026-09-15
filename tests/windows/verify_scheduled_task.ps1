<#
.SYNOPSIS
    Verifies the ConkyTempHelper scheduled task (created by installer/setup.iss's
    SetupTempHelper) points at a real, existing executable — not a path mangled
    by schtasks.exe's Command/Arguments splitter.

.DESCRIPTION
    schtasks.exe /Create re-parses its own /TR value into a Command + Arguments
    pair, and its splitter breaks a path at the first bare space *regardless of
    outer quoting* unless the executable path is wrapped in its own internal
    escaped quotes. A single layer of quotes around a path containing spaces
    (e.g. "C:\Program Files\Conky\...") silently produced
    Command="C:\Program" + Arguments="Files\...\lhm-temp.exe", so the task
    always failed with ERROR_FILE_NOT_FOUND — GPU/CPU sensor data never
    appeared, with no visible error anywhere. See conky-windows PORT_STATUS.md
    and issues #1/#2 for the full incident writeup (found + fixed 2026-09-15).

    This script queries the live task's XML (the ground truth — schtasks
    /Query's plain-text /V output can look fine even when the underlying
    Command/Arguments split is wrong) and asserts that Command + Arguments,
    concatenated, resolve to a file that actually exists.

.PARAMETER TaskName
    Scheduled task name. Default: ConkyTempHelper.

.PARAMETER ExpectedExePath
    If given, asserts the reconstructed path equals this exact path
    (case-insensitive). If omitted, only checks that the reconstructed path
    exists on disk.

.EXAMPLE
    powershell -File verify_scheduled_task.ps1
    powershell -File verify_scheduled_task.ps1 -ExpectedExePath "C:\Program Files\Conky\LibreHardwareMonitor\lhm-temp.exe"

.OUTPUTS
    Exit code 0 on success, 1 on any failure. Prints a clear PASS/FAIL line
    either way — suitable for both interactive use and CI.
#>
param(
    [string]$TaskName = "ConkyTempHelper",
    [string]$ExpectedExePath = ""
)

$ErrorActionPreference = "Stop"

function Fail($message) {
    Write-Host "FAIL: $message" -ForegroundColor Red
    exit 1
}

try {
    $xml = schtasks /Query /TN $TaskName /XML 2>&1
    if ($LASTEXITCODE -ne 0) {
        Fail "Could not query task '$TaskName': $xml"
    }
} catch {
    Fail "schtasks /Query threw: $_"
}

[xml]$taskXml = $xml -join "`n"
$ns = New-Object System.Xml.XmlNamespaceManager($taskXml.NameTable)
$ns.AddNamespace("t", "http://schemas.microsoft.com/windows/2004/02/mit/task")

$execNode = $taskXml.SelectSingleNode("//t:Actions/t:Exec", $ns)
if ($null -eq $execNode) {
    Fail "No <Exec> action found in task XML"
}

$command = $execNode.SelectSingleNode("t:Command", $ns).InnerText
$argsNode = $execNode.SelectSingleNode("t:Arguments", $ns)
$arguments = if ($argsNode) { $argsNode.InnerText } else { "" }

# Reconstruct the full path the way Windows actually would: Command and
# Arguments are just concatenated with a space when Command has no
# extension boundary recognized by schtasks's splitter — which is exactly
# the bug. A correctly-quoted task has the FULL path in Command and an
# empty (or genuinely separate) Arguments.
$reconstructed = if ($arguments) { "$command $arguments" } else { $command }

Write-Host "Task: $TaskName"
Write-Host "  Command:   $command"
Write-Host "  Arguments: $arguments"
Write-Host "  Reconstructed: $reconstructed"

if (-not (Test-Path -LiteralPath $command -PathType Leaf)) {
    Fail "Command '$command' does not point to an existing file. " + `
         "This is the exact schtasks /TR quoting bug (path split at a space) " + `
         "if Arguments looks like the tail of a path, e.g. 'Files\...\foo.exe'."
}

if ($ExpectedExePath -and ($command -ne $ExpectedExePath)) {
    Fail "Command '$command' does not match expected '$ExpectedExePath'"
}

if ($arguments) {
    Fail "Arguments should be empty for a correctly-quoted lhm-temp.exe task " + `
         "action, got: '$arguments' (this is the signature of the split-path bug)"
}

Write-Host "PASS: '$TaskName' points to an existing executable with no stray Arguments." -ForegroundColor Green
exit 0
