<#
.SYNOPSIS
Checks the DOS input path with DOSBox-X key injection.

.DESCRIPTION
Mirrors docs/verification.md. DOSBox 0.74 cannot inject keys, so DOSBox-X is
used: ADDKEY presses keys through the keyboard controller, which is what a real
user does. The input probe records what the guest sees; the demo is checked to
quit on Esc (the app callback) and on Alt+X (the backend policy). Alt has to be
held with ADDKEY's l<duration> form, because a plain ADDKEY a-x releases Alt too
quickly for the BIOS shift flags.

.EXAMPLE
./scripts/dos_input_check.ps1
#>
[CmdletBinding()]
param(
    [string] $OutDir = "build-dos",
    [string] $DosBoxX = "C:\DOSBox-X\dosbox-x.exe"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$out = Join-Path $repo $OutDir

function Invoke-DosBoxX {
    param([string] $Autoexec, [int] $WaitSeconds = 30)
    $conf = Join-Path $env:TEMP "libpxl-dosbox-x.conf"
    Set-Content $conf "[autoexec]`nmount c $out`nc:`n$Autoexec`nexit"
    $p = Start-Process -FilePath $DosBoxX -ArgumentList "-silent", "-conf", $conf -WorkingDirectory $out -PassThru
    if (-not $p.WaitForExit($WaitSeconds * 1000)) {
        $p.Kill()
        return $false
    }
    return $true
}

$failures = 0

# 1. the probe must see the injected keys and the Alt bit in the shift flags
Remove-Item (Join-Path $out "KEYLOG.TXT") -ErrorAction SilentlyContinue
Invoke-DosBoxX "addkey p2500 escape`nKEYPROB.EXE" | Out-Null
$log = Get-Content (Join-Path $out "KEYLOG.TXT") -ErrorAction SilentlyContinue
if (-not ($log -match "scan=1 ascii=1b")) { Write-Host "FAIL: no Esc key in KEYLOG.TXT"; $failures++ }

Remove-Item (Join-Path $out "KEYLOG.TXT") -ErrorAction SilentlyContinue
Invoke-DosBoxX "addkey p2500 l1000 a-x`nKEYPROB.EXE" | Out-Null
$log = Get-Content (Join-Path $out "KEYLOG.TXT") -ErrorAction SilentlyContinue
if (-not ($log -match "scan=2d")) { Write-Host "FAIL: no X key in KEYLOG.TXT"; $failures++ }
if (-not ($log -match "classic=[0-9a-f]*8")) { Write-Host "FAIL: the Alt bit is not set for Alt+X"; $failures++ }

# 2. the demo must quit on Esc (app callback) and on Alt+X (backend)
if (-not (Invoke-DosBoxX "addkey p2500 escape`nDEMO.EXE")) { Write-Host "FAIL: demo did not quit on Esc"; $failures++ }
if (-not (Invoke-DosBoxX "addkey p2500 l1000 a-x`nDEMO.EXE")) { Write-Host "FAIL: demo did not quit on Alt+X"; $failures++ }

if ($failures) { throw "$failures input check(s) failed" }
Write-Host "DOS input checks passed"
