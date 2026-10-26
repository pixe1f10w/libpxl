<#
.SYNOPSIS
Builds a libpxl target for DOS with OpenWatcom 2 or DJGPP.

.DESCRIPTION
Mirrors docs/verification.md. The toolchains are downloaded into $ToolRoot on
first use and are never installed system-wide. The DOS executable names stay
within 8.3 characters and the extender (DOS4GW.EXE or CWSDPMI.EXE) is copied
next to the executable, because DOSBox does not provide the DPMI hosts.

.PREREQUISITES
7-Zip on the PATH (used to unpack the OpenWatcom installer).

.EXAMPLE
./scripts/dos_build.ps1 -Toolchain watcom -Target smoke
./scripts/dos_build.ps1 -Toolchain djgpp -Target demo
#>
[CmdletBinding()]
param(
    [ValidateSet("watcom", "djgpp")] [string] $Toolchain = "watcom",
    [ValidateSet("demo", "smoke", "keyprobe")] [string] $Target = "smoke",
    [string] $ToolRoot = (Join-Path $env:LOCALAPPDATA "libpxl-dos-tools"),
    [string] $OutDir = "build-dos"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$out = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

$sources = @("libpxl.c", "libpxl_dos.c")
$targets = @{
    demo     = @{ File = "main.c";             Exe = "DEMO.EXE" }
    smoke    = @{ File = "tests/smoke_dos.c";   Exe = "SMOKE.EXE" }
    keyprobe = @{ File = "tests/keyprobe_dos.c"; Exe = "KEYPROB.EXE" }
}
$spec = $targets[$Target]

if ($Toolchain -eq "watcom") {
    $root = Join-Path $ToolRoot "ow2"
    if (-not (Test-Path (Join-Path $root "binnt64\wcc386.exe"))) {
        New-Item -ItemType Directory -Force -Path $ToolRoot | Out-Null
        $installer = Join-Path $ToolRoot "ow2.exe"
        Write-Host "downloading OpenWatcom 2 ..."
        curl.exe -L --fail -o $installer "https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/open-watcom-2_0-c-win-x64.exe"
        7z x $installer "-o$root" -y | Out-Null
    }
    $env:PATH = "$root\binnt64;$root\binw;$env:PATH"
    $env:LIB = "$root\lib386;$root\lib386\dos"
    $env:WATCOM = $root
    $objects = @()
    Push-Location $repo
    foreach ($source in $sources) {
        $obj = Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source)) + ".obj")
        & "$root\binnt64\wcc386.exe" -bt=dos "-i=$root\h" -w4 "-fo=$obj" $source
        if ($LASTEXITCODE -ne 0) { Pop-Location; throw "wcc386 failed for $source" }
        $objects += $obj
    }
    $obj = Join-Path $out "target.obj"
    & "$root\binnt64\wcc386.exe" -bt=dos "-i=$root\h" "-i=$repo" -w4 "-fo=$obj" $spec.File
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "wcc386 failed for $($spec.File)" }
    $objects += $obj
    & "$root\binnt64\wcl386.exe" -bt=dos -l=dos4g "-fe=$(Join-Path $out $spec.Exe)" @objects
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "wcl386 failed" }
    Pop-Location
    Copy-Item "$root\binw\dos4gw.exe" $out -Force
    Write-Host "built $out\$($spec.Exe) (OpenWatcom 2, DOS/4GW)"
} else {
    $pkg = Join-Path $ToolRoot "djgpp"
    if (-not (Test-Path (Join-Path $pkg "djgpp\bin\i586-pc-msdosdjgpp-gcc.exe"))) {
        New-Item -ItemType Directory -Force -Path $ToolRoot | Out-Null
        $zip = Join-Path $ToolRoot "djgpp.zip"
        Write-Host "downloading DJGPP cross toolchain ..."
        curl.exe -L --fail -o $zip "https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-mingw-gcc1220-standalone.zip"
        Expand-Archive -Path $zip -DestinationPath $pkg -Force
    }
    if (-not (Test-Path (Join-Path $out "CWSDPMI.EXE"))) {
        $zip = Join-Path $ToolRoot "cwsdpmi.zip"
        Write-Host "downloading CWSDPMI ..."
        curl.exe -L --fail -o $zip "http://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip"
        $tmp = Join-Path $ToolRoot "cwsdpmi"
        Expand-Archive -Path $zip -DestinationPath $tmp -Force
        Copy-Item "$tmp\bin\CWSDPMI.EXE" $out -Force
    }
    $gcc = Join-Path $pkg "djgpp\bin\i586-pc-msdosdjgpp-gcc.exe"
    $arguments = @("-O2", "-Wall", "-Wextra", "-Wpedantic", "-std=gnu99", "-I.")
    $arguments += $sources + @($spec.File)
    $arguments += @("-o", (Join-Path $out $spec.Exe))
    Push-Location $repo
    & $gcc @arguments
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "DJGPP build failed" }
    Pop-Location
    Write-Host "built $out\$($spec.Exe) (DJGPP, CWSDPMI)"
}
