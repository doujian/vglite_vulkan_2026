# run-tests.ps1 - Run vglite_vulkan_2026 test executables
# Usage:
#   .\run-tests.ps1                       # list available tests
#   .\run-tests.ps1 tiger                 # run one test (test_tiger.exe)
#   .\run-tests.ps1 clear, tiger          # run several tests
#   .\run-tests.ps1 -All                  # run ALL tests
#   .\run-tests.ps1 tiger -KeepGoing      # don't stop on first failure
#   .\run-tests.ps1 tiger -Full           # show full output (default: last 20 lines)
#
# Not part of the repository - dev-only convenience script.

param(
    [Parameter(Position=0)]
    [string[]]$Tests = @(),

    [switch]$All,
    [switch]$KeepGoing,
    [switch]$Full
)

$ErrorActionPreference = "Stop"

# --- Paths ---
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$TestsDir  = Join-Path $ScriptDir "build\tests"

# --- Toolchain PATH (MinGW runtime DLLs needed by .exe, Vulkan loader, etc.) ---
$ToolchainPath = @(
    "C:\Users\HUAWEI\tools\mingw64\bin",
    "C:\Users\HUAWEI\tools\glslang\bin"
) -join ";"
$env:Path = "$ToolchainPath;$env:Path"

if (-not (Test-Path $TestsDir)) {
    Write-Host "[ERROR] build\tests not found. Run .\build.ps1 first." -ForegroundColor Red
    exit 1
}

# --- Discover available tests ---
$allExes = Get-ChildItem -Path $TestsDir -Filter "test_*.exe" |
           Sort-Object Name
$available = $allExes | ForEach-Object { $_.BaseName -replace "^test_", "" }

# --- Decide which to run ---
if ($All) {
    $toRun = $available
} elseif ($Tests.Count -gt 0) {
    $toRun = @()
    foreach ($t in $Tests) {
        $t = $t.Trim() -replace "^test_", ""
        if ($available -contains $t) {
            $toRun += $t
        } else {
            Write-Host "[WARN] test '$t' not found, skipping." -ForegroundColor Yellow
        }
    }
} else {
    # No args: just list and exit
    Write-Host "Available tests in $TestsDir" -ForegroundColor Cyan
    Write-Host ("  {0} tests total" -f $available.Count)
    $available | ForEach-Object { Write-Host "  - $_" }
    Write-Host ""
    Write-Host "Usage:" -ForegroundColor Cyan
    Write-Host "  .\run-tests.ps1 <name>[,<name>...]"
    Write-Host "  .\run-tests.ps1 -All"
    Write-Host "  .\run-tests.ps1 tiger -KeepGoing -Full"
    exit 0
}

if ($toRun.Count -eq 0) {
    Write-Host "[ERROR] No matching tests to run." -ForegroundColor Red
    exit 1
}

# --- Run ---
Push-Location $TestsDir
try {
    $passed = 0
    $failed = 0
    $results = @()

    Write-Host ("Running {0} test(s) from {1}`n" -f $toRun.Count, $TestsDir) -ForegroundColor Cyan

    foreach ($name in $toRun) {
        $exe = "test_$name.exe"
        Write-Host ("=== {0} ===" -f $exe) -ForegroundColor Cyan

        # Capture output
        $output = & ".\$exe" 2>&1 | Out-String
        $exit   = $LASTEXITCODE

        if ($Full) {
            Write-Host $output
        } else {
            $lines = $output -split "`r?`n"
            if ($lines.Count -gt 20) {
                Write-Host "[... $($lines.Count - 20) lines truncated, use -Full to see all ...]" -ForegroundColor DarkGray
                $lines | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }
            } else {
                Write-Host $output
            }
        }

        if ($exit -eq 0) {
            Write-Host ("[PASS] {0} (exit 0)`n" -f $exe) -ForegroundColor Green
            $passed++
            $results += [pscustomobject]@{ Test=$name; Result="PASS"; Exit=$exit }
        } else {
            Write-Host ("[FAIL] {0} (exit {1})`n" -f $exe, $exit) -ForegroundColor Red
            $failed++
            $results += [pscustomobject]@{ Test=$name; Result="FAIL"; Exit=$exit }
            if (-not $KeepGoing) {
                Write-Host "Stopping (use -KeepGoing to continue past failures)" -ForegroundColor Yellow
                break
            }
        }
    }

    # --- Summary ---
    Write-Host "=== Summary ===" -ForegroundColor Cyan
    $results | Format-Table -AutoSize
    Write-Host ("Total: {0}  Passed: {1}  Failed: {2}" -f $toRun.Count, $passed, $failed)
    if ($failed -gt 0) { exit 1 }
}
finally {
    Pop-Location
}
