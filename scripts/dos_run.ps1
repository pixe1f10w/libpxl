<#
.SYNOPSIS
Runs a DOS build in DOSBox 0.74 and checks the result files.

.DESCRIPTION
Mirrors docs/verification.md. The DOS program writes its result files itself,
because DOSBox does not pass the guest exit code to the host. With -Capture the
emulated screen is captured to a PNG after a few seconds, which is how the demo
rendering is compared against the host build.

.EXAMPLE
./scripts/dos_run.ps1 -Target smoke -Check
./scripts/dos_run.ps1 -Target demo -Capture -Shot build-dos/demo-dos.png
#>
[CmdletBinding()]
param(
    [ValidateSet("demo", "smoke", "keyprobe")] [string] $Target = "smoke",
    [string] $OutDir = "build-dos",
    [string] $DosBox = "C:\Program Files (x86)\DOSBox-0.74-3\DOSBox.exe",
    [switch] $Check,
    [switch] $Capture,
    [string] $Shot,
    [int] $Seconds = 4
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$out = Join-Path $repo $OutDir
$exe = @{ demo = "DEMO.EXE"; smoke = "SMOKE.EXE"; keyprobe = "KEYPROB.EXE" }[$Target]
$results = @{
    demo     = @()
    smoke    = @("SMOKE1.TXT", "SMOKE2.TXT", "SMOKE3.TXT")
    keyprobe = @("KEYLOG.TXT")
}[$Target]

foreach ($file in $results) {
    Remove-Item (Join-Path $out $file) -ErrorAction SilentlyContinue
}

$command = "`"$DosBox`" -c `"mount c $out`" -c `"c:`" -c `"$exe`" -c `"exit`" -exit"

if ($Capture) {
    Add-Type -AssemblyName System.Drawing
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
}
"@
    $p = Start-Process -FilePath $DosBox -ArgumentList "-c", "mount c $out", "-c", "c:", "-c", $exe, "-c", "exit", "-exit" -WorkingDirectory $out -PassThru
    Start-Sleep -Seconds $Seconds
    $p.Refresh()
    $handle = $p.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force; throw "no DOSBox window" }
    [Win]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 500
    $rect = New-Object Win+RECT
    [Win]::GetClientRect($handle, [ref]$rect) | Out-Null
    $point = New-Object Win+POINT
    [Win]::ClientToScreen($handle, [ref]$point) | Out-Null
    $w = $rect.R - $rect.L
    $h = $rect.B - $rect.T
    if (-not $Shot) { $Shot = Join-Path $out "$Target-dos.png" }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($point.X, $point.Y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "captured $w x $h to $Shot"
    Stop-Process -Id $p.Id -Force
} else {
    cmd /c $command | Out-Null
}

if ($Check) {
    if ($Target -eq "smoke") {
        if ((Get-Content (Join-Path $out "SMOKE2.TXT")) -ne "0") { throw "libpxl open failed on DOS" }
        if ((Get-Content (Join-Path $out "SMOKE3.TXT")) -ne "3") { throw "expected 3 frames on DOS" }
        Write-Host "DOS smoke test passed"
    }
}
