<#
.SYNOPSIS
    Runs an executable elevated (RunLevel=HighestAvailable), via a one-shot
    scheduled task, without triggering an interactive UAC consent prompt.

.DESCRIPTION
    setup.iss has PrivilegesRequired=admin, so directly launching the built
    installer (or its uninstaller) with Start-Process hangs forever on a
    headless CI runner: the admin manifest triggers a real UAC consent
    dialog with no one to click it (confirmed the hard way in CI - a first
    attempt at this sat for 47 minutes before being cancelled). Route
    through a one-shot elevated scheduled task instead - the same
    technique setup.iss itself uses for the ConkyTempHelper task, which
    doesn't prompt interactively.

    Builds the task from an explicit XML definition rather than schtasks'
    /TR string parameter: schtasks re-parses /TR into Command+Arguments
    using its own splitter (the exact bug fixed in d8ee2dec), AND
    Windows PowerShell 5.1's native-argument marshalling for a string
    containing embedded escaped quotes is unreliable in practice (confirmed
    locally: a manually-quoted /TR value produced "Invalid argument/option"
    errors from a substring of the path bleeding out as a bare token, even
    though the same string looked correct in isolation). The XML path
    sidesteps both problems entirely - Command and Arguments are separate,
    unambiguous XML elements, no string-splitting heuristics involved.

    NOTE: creating a RunLevel=HighestAvailable task itself requires the
    *creating* process to already be elevated (confirmed locally: fails
    with "Access is denied" from a Medium-integrity session). Whether the
    calling CI job's process is itself elevated enough for this to work is
    exactly the open question this script exists to answer empirically  - 
    see conky-windows issue #2.

.PARAMETER ExePath
    Path to the executable to run elevated.

.PARAMETER Arguments
    Argument string to pass to it (may be empty).

.PARAMETER WaitForPath
    If given, poll for this path to exist (success marker) instead of
    polling for the task's own state.

.PARAMETER WaitWhilePathExists
    If given, poll for this path to stop existing (e.g. an uninstall
    target directory disappearing) instead of polling for the task state.

.PARAMETER TimeoutSeconds
    Max time to wait for WaitForPath / WaitWhilePathExists. Default 180.

.EXAMPLE
    installer\run_elevated.ps1 -ExePath "C:\path\Setup.exe" `
        -Arguments "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART" `
        -WaitForPath "C:\Program Files\Conky\conky.exe"
#>
param(
    [Parameter(Mandatory = $true)][string]$ExePath,
    [string]$Arguments = "",
    [string]$WaitForPath,
    [string]$WaitWhilePathExists,
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"

$taskName = "ConkyCIElevatedRun_" + [Guid]::NewGuid().ToString("N").Substring(0, 8)
$xmlPath = Join-Path $env:TEMP "$taskName.xml"

$escExe = [System.Security.SecurityElement]::Escape($ExePath)
$argsElement = ""
if ($Arguments) {
    $escArgs = [System.Security.SecurityElement]::Escape($Arguments)
    $argsElement = "<Arguments>$escArgs</Arguments>"
}

$xml = @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <Triggers />
  <Principals>
    <Principal id="Author">
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>$escExe</Command>
      $argsElement
    </Exec>
  </Actions>
</Task>
"@
Set-Content -Path $xmlPath -Value $xml -Encoding Unicode

try {
    Write-Host "Creating elevated task '$taskName' for: $ExePath $Arguments"
    schtasks /Create /TN $taskName /XML $xmlPath /F
    if ($LASTEXITCODE -ne 0) {
        throw "schtasks /Create failed (exit $LASTEXITCODE) -- creating process likely isn't elevated enough to create a HighestAvailable task."
    }

    schtasks /Run /TN $taskName
    if ($LASTEXITCODE -ne 0) {
        throw "schtasks /Run failed (exit $LASTEXITCODE)"
    }

    if ($WaitForPath) {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while (-not (Test-Path $WaitForPath) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 5
        }
        if (-not (Test-Path $WaitForPath)) {
            throw "Timed out after ${TimeoutSeconds}s waiting for '$WaitForPath' to appear."
        }
        Write-Host "Confirmed: $WaitForPath exists."
    } elseif ($WaitWhilePathExists) {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Test-Path $WaitWhilePathExists) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 5
        }
        if (Test-Path $WaitWhilePathExists) {
            throw "Timed out after ${TimeoutSeconds}s waiting for '$WaitWhilePathExists' to disappear."
        }
        Write-Host "Confirmed: $WaitWhilePathExists no longer exists."
    } else {
        # No marker given -- just give it a moment to start.
        Start-Sleep -Seconds 5
    }
} finally {
    schtasks /Delete /TN $taskName /F 2>&1 | Out-Null
    Remove-Item $xmlPath -ErrorAction SilentlyContinue
}
