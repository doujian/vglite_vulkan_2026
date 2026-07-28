# Project Guidelines

## Test Requirement

Every code change MUST be followed by running the test cases to ensure no regression compared to the previous state.

### Blit Test Matrix

Blit-related changes (shader, pipeline, vg_lite_blit, push constants) MUST pass all 4 configurations:

| Config | VGLITE_BLIT_MSAA | VGLITE_BLIT_OBB | Description |
|--------|-------------------|------------------|-------------|
| 1 | 1 | 1 | 4x MSAA + OBB quad (default) |
| 2 | 1 | 0 | 4x MSAA + fullscreen triangle |
| 3 | 0 | 1 | 1x no-MSAA + OBB quad |
| 4 | 0 | 0 | 1x no-MSAA + fullscreen triangle |

Rebuild between configs: change macros in `inc/vg_lite_config.h`, run `cmake --build build`, run full test suite from `build/tests/Debug/`. All 4 configs must show the same pass/fail counts (only `test_sft_blit` pre-existing crash allowed).

## Bugfix Logging

Every bug fix MUST be recorded in `FIXES.md` with three sections: **Symptom** (what went wrong), **Root Cause** (why it happened), and **Solution** (what was changed and where). Append new entries at the bottom.

## Pre-Push Checklist

Before every `git push`, check whether `README.md` needs updating (new features, test status changes, usage instructions, etc.). If changes are needed, update and commit before pushing.

## Reference Resources

- **CTS Test Cases**: `/home/panda/VGLite_Tests` — Original VGLite CTS test cases used for validation.
- **Reference Implementation**: `/home/panda/gpu-vglite` — Original VGLite implementation, available for reference.
