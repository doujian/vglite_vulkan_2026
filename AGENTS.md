# Project Guidelines

## Test Requirement

Every code change MUST be followed by running the test cases to ensure no regression compared to the previous state.

## Bugfix Logging

Every bug fix MUST be recorded in `FIXES.md` with three sections: **Symptom** (what went wrong), **Root Cause** (why it happened), and **Solution** (what was changed and where). Append new entries at the bottom.

## Pre-Push Checklist

Before every `git push`, check whether `README.md` needs updating (new features, test status changes, usage instructions, etc.). If changes are needed, update and commit before pushing.

## Reference Resources

- **CTS Test Cases**: `/home/panda/VGLite_Tests` — Original VGLite CTS test cases used for validation.
- **Reference Implementation**: `/home/panda/gpu-vglite` — Original VGLite implementation, available for reference.
