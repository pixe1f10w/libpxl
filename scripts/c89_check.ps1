<#
.SYNOPSIS
Checks that the library sources stay C89-clean.

.DESCRIPTION
The library must build with the default C89 mode of OpenWatcom 2, so the sources
avoid C99 constructs. This runs a strict C89 compile with clang-cl (the compiler
must find the Windows SDK headers, so run it from a Visual Studio developer
environment; in CI the ilammy/msvc-dev-cmd action provides one).

.EXAMPLE
./scripts/c89_check.ps1
#>
[CmdletBinding()]
param(
    [string[]] $Files = @("libpxl.c", "libpxl_win32.c", "main.c"),
    [string] $Compiler = "clang-cl"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$out = Join-Path ([IO.Path]::GetTempPath()) "libpxl-c89"
New-Item -ItemType Directory -Force -Path $out | Out-Null

if (-not (Get-Command $Compiler -ErrorAction SilentlyContinue)) {
    throw "$Compiler not found; run this from a Visual Studio developer shell (or use CI)"
}

Push-Location $repo
$failures = 0
foreach ($file in $Files) {
    $obj = Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($file)) + ".obj")
    & $Compiler /TC /W4 /c "-o$obj" "/clang:-std=c89" "/clang:-pedantic-errors" $file
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: $file is not C89-clean"
        $failures++
    }
}
Pop-Location

if ($failures) { throw "$failures file(s) are not C89-clean" }
Write-Host "C89 check passed"
