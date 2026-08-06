# Project Guidelines

## Test Requirement

Every code change MUST be followed by running the test cases to ensure no regression compared to the previous state.

### Full Test Matrix (3 axes × 8 configs)

ALL code changes MUST pass all 8 configurations (3 compile-time axes: Tiling × MSAA × OBB):

| Config | VGLITE_TARGET_OPTIMAL | VGLITE_BLIT_MSAA | VGLITE_BLIT_OBB | Build Dir | Dump Dir | Description |
|--------|-----------------------|-------------------|------------------|-----------|----------|-------------|
| 1 | OFF | ON | ON | `build/` | `dump_lin_msaa_obb/` | LINEAR + 4x MSAA + OBB (default) |
| 2 | OFF | ON | OFF | `build_lin_msaa_noobb/` | `dump_lin_msaa_noobb/` | LINEAR + 4x MSAA + fullscreen tri |
| 3 | OFF | OFF | ON | `build_lin_nomsaa_obb/` | `dump_lin_nomsaa_obb/` | LINEAR + 1x + OBB quad |
| 4 | OFF | OFF | OFF | `build_lin_nomsaa_noobb/` | `dump_lin_nomsaa_noobb/` | LINEAR + 1x + fullscreen tri |
| 5 | ON | ON | ON | `build_tiled/` | `dump_opt_msaa_obb/` | OPTIMAL + 4x MSAA + OBB |
| 6 | ON | ON | OFF | `build_opt_msaa_noobb/` | `dump_opt_msaa_noobb/` | OPTIMAL + 4x MSAA + fullscreen tri |
| 7 | ON | OFF | ON | `build_opt_nomsaa_obb/` | `dump_opt_nomsaa_obb/` | OPTIMAL + 1x + OBB quad |
| 8 | ON | OFF | OFF | `build_opt_nomsaa_noobb/` | `dump_opt_nomsaa_noobb/` | OPTIMAL + 1x + fullscreen tri |

Build and test commands:
```
# Config 1 (default): LINEAR + MSAA + OBB
cmake -B build -DVGLITE_TARGET_OPTIMAL=OFF -DVGLITE_BLIT_MSAA=ON -DVGLITE_BLIT_OBB=ON
cmake --build build
# run all tests from build/tests/Debug/

# Config 2-8: same pattern, change flags + build dir
# Example — Config 8: OPTIMAL + no MSAA + no OBB
cmake -B build_opt_nomsaa_noobb -DVGLITE_TARGET_OPTIMAL=ON -DVGLITE_BLIT_MSAA=OFF -DVGLITE_BLIT_OBB=OFF
cmake --build build_opt_nomsaa_noobb
# run all tests from build_opt_nomsaa_noobb/tests/Debug/
```

All 8 configs must show **37/38 PASS** (only `test_sft_blit` pre-existing crash allowed).

PNG/raw outputs are automatically routed to configuration-specific subdirectories (e.g. `dump_lin_msaa_obb/`, `dump_opt_nomsaa_noobb/`) for visual comparison.

## Bugfix Logging

Every bug fix MUST be recorded in `FIXES.md` with three sections: **Symptom** (what went wrong), **Root Cause** (why it happened), and **Solution** (what was changed and where). Append new entries at the bottom.

## Pre-Push Checklist

Before every `git push`, check whether `README.md` needs updating (new features, test status changes, usage instructions, etc.). If changes are needed, update and commit before pushing.

## Reference Resources

- **CTS Test Cases**: `/home/panda/VGLite_Tests` — Original VGLite CTS test cases used for validation.
- **Reference Implementation**: `/home/panda/gpu-vglite` — Original VGLite implementation, available for reference.
