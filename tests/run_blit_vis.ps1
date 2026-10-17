# run_blit_vis.ps1 - one-command BLIT OBB visualization for a single test run.
#
# Usage: powershell -File tests/run_blit_vis.ps1 -Test test_blit_multi [-OutDir <dir>]
#
# Runs the given test exe with VGLITE_DUMP_GEOMETRY=1 (lavapipe ICD if unset),
# then renders ONLY the BLIT records of the fresh dump into <TestBaseName>_blit_vis.png
# next to the test exe (build/tests/Debug/).

param(
    [Parameter(Mandatory = $true)] [string] $Test,
    [string] $OutDir
)

$ErrorActionPreference = 'Stop'

# --- locate repo root (parent of tests\) and the default build dir ---
$repoRoot = Split-Path -Parent $PSScriptRoot
$runDir = Join-Path $repoRoot 'build\tests\Debug'

# --- resolve test exe ---
$exe = $Test
if (-not [System.IO.Path]::IsPathRooted($exe)) {
    $exe = Join-Path $runDir $Test
}
if ($exe -notmatch '\.exe$') { $exe = "$exe.exe" }
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "Test exe not found: $exe"
    exit 1
}
$testBase = [System.IO.Path]::GetFileNameWithoutExtension($exe)

# --- visualizer ---
$viz = Join-Path $runDir 'geom_visualizer.exe'
if (-not (Test-Path -LiteralPath $viz)) {
    Write-Error "geom_visualizer.exe not found at $viz (build it first: cmake --build build --target geom_visualizer)"
    exit 1
}

# --- env ---
$env:VGLITE_DUMP_GEOMETRY = '1'
if (-not $env:VK_DRIVER_FILES) {
    $env:VK_DRIVER_FILES = 'D:\tools\mesa\lvp\x64\lvp_icd.x86_64.json'
}

# --- delete stale dumps so only this run's records remain ---
Get-ChildItem -Path $runDir -Filter 'dump_*' -Directory -ErrorAction SilentlyContinue |
    ForEach-Object {
        Get-ChildItem -Path $_.FullName -Filter 'geometry_dump.txt' -File -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }

# --- run the test ---
Write-Host "Running: $exe (CWD: $runDir)"
Push-Location $runDir
try {
    & $exe
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Test failed with exit code $LASTEXITCODE"
        Pop-Location
        exit 1
    }
} finally {
    Pop-Location
}

# --- locate the fresh dump (newest under dump_*/) ---
$dump = Get-ChildItem -Path $runDir -Filter 'dump_*' -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ChildItem -Path $_.FullName -Filter 'geometry_dump.txt' -File -ErrorAction SilentlyContinue } |
    Sort-Object LastWriteTime |
    Select-Object -Last 1
if (-not $dump) {
    Write-Error "No geometry_dump.txt produced under $runDir\dump_*\ (was VGLITE_DUMP_GEOMETRY honored?)"
    exit 1
}

# --- output dir: default = test exe dir (build\tests\Debug) ---
if (-not $OutDir) { $OutDir = $runDir }
$png = Join-Path $OutDir "$($testBase)_blit_vis.png"

# --- render BLIT-only visualization ---
& $viz $dump.FullName $png 'blit'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $png)) {
    Write-Error "geom_visualizer failed for dump '$($dump.FullName)'"
    exit 1
}

Write-Host ""
Write-Host "PNG: $([System.IO.Path]::GetFullPath($png))"
Write-Host "DUMP: $($dump.FullName)"
exit 0
