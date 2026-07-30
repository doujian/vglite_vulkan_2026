<#
.SYNOPSIS
    Build script for vglite_vulkan_2026 (MinGW + CMake, Debug).
    Not under git management — local helper for development.

.DESCRIPTION
    - Detects MinGW + CMake from common install paths (with PATH fallback).
    - Uses generator "MinGW Makefiles" and CMAKE_BUILD_TYPE=Debug.
    - Idempotent: reuses existing build/ cache if present, else configures fresh.
    - Parallel build (-j8 by default).
    - Builds the 'all' target (38 test_*.exe + the static vg_lite lib).

.PARAMETER Clean
    Wipe build/ before configuring (full rebuild from scratch).

.PARAMETER Jobs
    Parallel job count for mingw32-make (default 8).

.PARAMETER BuildType
    CMAKE_BUILD_TYPE value (Debug, Release, RelWithDebInfo). Default Debug.

.PARAMETER Target
    Specific target to build instead of default 'all' (e.g. test_radialGrad).

.EXAMPLE
    .\build.local.ps1
    .\build.local.ps1 -Clean
    .\build.local.ps1 -Target test_radialGrad
    .\build.local.ps1 -BuildType Release -Jobs 16
#>

[CmdletBinding()]
param(
    [switch]$Clean,
    [int]$Jobs = 8,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$BuildType = 'Debug',
    [string]$Target
)

$ErrorActionPreference = 'Continue'

# --- Locate project root (script dir) ---
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $ScriptDir
Write-Host "[build] project root: $ScriptDir" -ForegroundColor Cyan

# --- Locate toolchain ---
function Find-Exe {
    param([string]$Name, [string[]]$HintPaths)
    foreach ($p in $HintPaths) {
        $full = Join-Path $p $Name
        if (Test-Path -LiteralPath $full) { return $full }
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$MingwDir = 'C:\Users\HUAWEI\tools\mingw64\bin'
$CmakeDir = 'C:\Users\HUAWEI\tools\cmake-3.30.5-windows-x86_64\bin'

$gcc = Find-Exe 'gcc.exe' @($MingwDir)
$gxx = Find-Exe 'g++.exe' @($MingwDir)
$make = Find-Exe 'mingw32-make.exe' @($MingwDir)
$cmake = Find-Exe 'cmake.exe' @($CmakeDir)

if (-not $gcc -or -not $gxx -or -not $make -or -not $cmake) {
    Write-Host "[build] toolchain detection FAILED:" -ForegroundColor Red
    Write-Host "  gcc   : $gcc"
    Write-Host "  g++   : $gxx"
    Write-Host "  make  : $make"
    Write-Host "  cmake : $cmake"
    Write-Host ""
    Write-Host "Install or edit toolchain paths at top of this script." -ForegroundColor Yellow
    exit 1
}

Write-Host "[build] gcc   : $gcc"
Write-Host "[build] g++   : $gxx"
Write-Host "[build] make  : $make"
Write-Host "[build] cmake : $cmake"

# --- Paths ---
$BuildDir = Join-Path $ScriptDir 'build'

# --- Clean ---
if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Host "[build] -Clean: removing $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force -LiteralPath $BuildDir
}

# --- Configure (only if cache missing) ---
$CacheFile = Join-Path $BuildDir 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $CacheFile)) {
    Write-Host "[build] configuring fresh CMake cache..." -ForegroundColor Cyan
    & $cmake -S $ScriptDir -B $BuildDir `
        -G "MinGW Makefiles" `
        "-DCMAKE_C_COMPILER=$gcc" `
        "-DCMAKE_CXX_COMPILER=$gxx" `
        "-DCMAKE_MAKE_PROGRAM=$make" `
        "-DCMAKE_BUILD_TYPE=$BuildType"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[build] CMake configure FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}
else {
    Write-Host "[build] reusing existing CMake cache at $BuildDir" -ForegroundColor DarkGray
    # Honor BuildType change on existing cache
    & $cmake -S $ScriptDir -B $BuildDir "-DCMAKE_BUILD_TYPE=$BuildType" | Out-Null
}

# --- Build ---
$targetArg = if ($Target) { $Target } else { 'all' }
Write-Host "[build] invoking mingw32-make -j $Jobs target=$targetArg" -ForegroundColor Cyan
# Redirect mingw32-make stderr (2>&1) into merged output so PS NativeCommandError noise is suppressed.
& $make -C $BuildDir "-j$Jobs" $targetArg 2>&1 | ForEach-Object {
    if ($_ -is [System.Management.Automation.ErrorRecord]) {
        Write-Host $_.Exception.Message
    } else {
        Write-Host $_
    }
}
$buildExit = $LASTEXITCODE

if ($buildExit -eq 0) {
    Write-Host "[build] SUCCESS" -ForegroundColor Green
    $exes = Get-ChildItem -Path (Join-Path $BuildDir 'tests') -Filter 'test_*.exe' -ErrorAction SilentlyContinue
    Write-Host "[build] $($exes.Count) test_*.exe in $BuildDir\tests\"
}
else {
    Write-Host "[build] FAILED (exit $buildExit)" -ForegroundColor Red
}
exit $buildExit
