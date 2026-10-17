#!/usr/bin/env bash
# run_blit_vis.sh - one-command BLIT OBB visualization for a single test run.
# Bash/Linux port of tests/run_blit_vis.ps1 (same behavior, driver-agnostic).
#
# Usage: tests/run_blit_vis.sh -Test test_blit_multi [-OutDir <dir>]
#        tests/run_blit_vis.sh test_blit_multi            (positional form)
#
# Env:   RUN_BLIT_VIS_ROOT  repo root override (default: parent of this script's dir)
#
# Runs the given test exe with VGLITE_DUMP_GEOMETRY=1, then renders ONLY the
# BLIT records of the fresh dump into <TestBaseName>_blit_vis.png next to the
# test exe (build/tests/Debug/). VK_DRIVER_FILES is NOT set by this script; if
# it is unset a note is printed (set it yourself for a software driver).

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: run_blit_vis.sh -Test <name-or-path> [-OutDir <dir>]
       run_blit_vis.sh <name-or-path> [-OutDir <dir>]

  -Test, -t <name-or-path>  Test exe: bare name -> <root>/build/tests/Debug/<name>
                            (falls back to <name>.exe if only that exists),
                            or an absolute/relative path to the exe.
  -OutDir, -o <dir>         PNG output dir (default: the test exe's dir).
  -h, --help                Show this help.

Env:
  RUN_BLIT_VIS_ROOT         Repo root (default: parent of the script's directory).
EOF
}

die() { echo "ERROR: $*" >&2; exit 1; }

TEST=""
OUTDIR=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -Test|-t|--test)
            [[ $# -ge 2 ]] || die "-Test requires a value"
            TEST="$2"; shift 2 ;;
        -OutDir|-o|--out-dir)
            [[ $# -ge 2 ]] || die "-OutDir requires a value"
            OUTDIR="$2"; shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do
                [[ -z "$TEST" ]] || die "unexpected extra argument: $1"
                TEST="$1"; shift
            done ;;
        -*)
            echo "ERROR: unknown option: $1" >&2; usage >&2; exit 1 ;;
        *)
            [[ -z "$TEST" ]] || die "unexpected extra argument: $1"
            TEST="$1"; shift ;;
    esac
done
[[ -n "$TEST" ]] || { usage >&2; die "-Test <name> is required"; }

# --- locate repo root (parent of tests/) and the default build dir ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${RUN_BLIT_VIS_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
RUN_DIR="$REPO_ROOT/build/tests/Debug"
[[ -d "$RUN_DIR" ]] || die "build dir not found: $RUN_DIR"

# --- resolve test exe (prefer plain name, fall back to .exe) ---
exe="$TEST"
if [[ "$exe" != /* ]]; then
    exe="$RUN_DIR/$TEST"
fi
if [[ ! -x "$exe" && -x "$exe.exe" && "$exe" != *.exe ]]; then
    exe="$exe.exe"
fi
[[ -f "$exe" ]] || die "Test exe not found: $exe"
testBase="$(basename "$exe")"
testBase="${testBase%.*}"

# --- visualizer (prefer plain name, fall back to .exe) ---
viz="$RUN_DIR/geom_visualizer"
if [[ ! -f "$viz" && -f "$viz.exe" ]]; then
    viz="$viz.exe"
fi
[[ -f "$viz" ]] || die "geom_visualizer not found at $viz (build it first: cmake --build build --target geom_visualizer)"

# --- env ---
export VGLITE_DUMP_GEOMETRY=1
if [[ -z "${VK_DRIVER_FILES:-}" ]]; then
    echo "NOTE: VK_DRIVER_FILES is not set; leaving it unset. If you need a software"
    echo "NOTE: driver (e.g. lavapipe), set VK_DRIVER_FILES to its ICD JSON and rerun."
fi

# --- delete stale dumps so only this run's records remain ---
find "$RUN_DIR" -maxdepth 1 -type d -name 'dump_*' \
    -exec find {} -maxdepth 1 -type f -name 'geometry_dump.txt' -delete \; 2>/dev/null || true

# --- run the test ---
echo "Running: $exe (CWD: $RUN_DIR)"
(
    cd "$RUN_DIR"
    "$exe" # absolute path (either given or resolved under RUN_DIR)
) || die "Test failed with exit code $?"

# --- locate the fresh dump (newest under dump_*/) ---
dump="$(find "$RUN_DIR" -maxdepth 2 -type f -name 'geometry_dump.txt' -path '*/dump_*/*' -printf '%T@ %p\n' 2>/dev/null \
    | sort -n | tail -n 1 | cut -d' ' -f2- || true)"
[[ -n "$dump" && -f "$dump" ]] || die "No geometry_dump.txt produced under $RUN_DIR/dump_*/ (was VGLITE_DUMP_GEOMETRY honored?)"

# --- output dir: default = test exe dir (build/tests/Debug) ---
if [[ -z "$OUTDIR" ]]; then OUTDIR="$(dirname "$exe")"; fi
mkdir -p "$OUTDIR"
png="$OUTDIR/${testBase}_blit_vis.png"

# --- render BLIT-only visualization (propagate exit code; it errors on no-BLIT dumps) ---
(
    cd "$(dirname "$viz")"
    "./$(basename "$viz")" "$dump" "$png" blit
) || die "geom_visualizer failed for dump '$dump'"
[[ -f "$png" ]] || die "geom_visualizer failed for dump '$dump' (no PNG produced)"

echo ""
echo "PNG: $(cd "$(dirname "$png")" && pwd)/$(basename "$png")"
echo "DUMP: $(cd "$(dirname "$dump")" && pwd)/$(basename "$dump")"
exit 0
