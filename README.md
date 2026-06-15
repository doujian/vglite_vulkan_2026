# vglite_by_vulkan

VGLite API implementation based on Vulkan. Provides `vg_lite_clear`, `vg_lite_blit`, `vg_lite_draw`, and related functionality using Vulkan graphics pipeline, compatible with the VGLite API interface.

## Architecture

```
inc/vg_lite.h            - VGLite public API declarations
inc/vg_lite_util.h       - Utility API declarations
src/vg_lite.c            - Main API implementation (clear, blit, init, finish, matrix, vg_lite_init_path)
src/vg_lite_vulkan.c     - Vulkan device/context/pipeline/scissor management
src/vg_lite_vulkan.h     - Vulkan context, buffer_internal_t, scissor state
src/vg_lite_draw.c       - Path drawing (fill, stroke, pattern, gradient)
src/vg_lite_gradient.c   - Linear/radial gradient texture generation
src/vg_lite_format.c     - Pixel format conversion (format→Vulkan mapping, bpp)
src/vg_lite_math.h       - 3x3 matrix helpers (mat3_multiply, mat3_inverse)
src/vlc_parser.c         - VLC path data parser (S8/S16/S32/FP32, absolute+relative opcodes)
src/tessellator.c        - Path tessellation (triangulation, even-odd/non-zero fill)
shaders/blit.vert        - Blit vertex shader (full-screen triangle)
shaders/blit.frag        - Blit fragment shader (shader-blend path, MSAA)
shaders/blit_native.frag - Blit fragment shader (pipeline-blend path, no MSAA)
shaders/draw.vert        - Draw vertex shader
shaders/draw.frag        - Draw fragment shader
shaders/gradient.vert    - Linear gradient vertex shader
shaders/gradient.frag    - Linear gradient fragment shader
shaders/pattern.vert     - Pattern fill vertex shader
shaders/pattern.frag     - Pattern fill fragment shader
src/spv_*.h              - Embedded SPIR-V shader headers (auto-generated)
util/util.c              - Test utility: expected buffer, gen_image, pack/read pixel, CPU gradient sim
util/vg_lite_util.c      - PNG save/load, buffer allocation helper
util/Common.h            - Shared test macros (CHECK_ERROR, IS_ERROR)
docs/spv-regeneration.md - SPV shader rebuild guide and troubleshooting
docs/vg_lite_draw.md     - vg_lite_draw API documentation
```

## Supported Features

- **vg_lite_init / vg_lite_close** - Vulkan device initialization and cleanup
- **vg_lite_allocate / vg_lite_free** - Buffer allocation via Vulkan memory
- **vg_lite_clear** - Full or rectangle clear with solid color
- **vg_lite_blit** - Blit with 3x3 matrix transform, format conversion, blend modes
- **vg_lite_draw** - Path fill with tessellation (non-zero / even-odd fill rules)
- **vg_lite_init_path** - Programmatic path creation (bounding box, quality, format, data)
- **vg_lite_draw_grad** - Linear gradient fill with dedicated Vulkan shaders
- **vg_lite_draw_radial_grad** - Radial gradient fill
- **vg_lite_draw_pattern** - Pattern image fill with transform and blend
- **vg_lite_set_CLUT** - 256-entry color lookup table for INDEX_8 format
- **Scissor clip** - vg_lite_set_scissor, vg_lite_scissor_rects (multi-rect), enable/disable
- **Matrix ops** - identity, translate, scale, rotate
- **Blend modes**: NONE, SRC_OVER, DST_OVER, SRC_IN, DST_IN, MULTIPLY, SCREEN, DARKEN, LIGHTEN, ADDITIVE, SUBTRACT, NORMAL_LVGL, ADDITIVE_LVGL, SUBTRACT_LVGL, MULTIPLY_LVGL, OpenVG premultiplied modes
- **Image modes**: NONE (color only), NORMAL, MULTIPLY, STENCIL, RECOLOR
- **Pixel formats**: RGBA8888, BGRA8888, RGBX8888, BGRX8888, RGB565, BGR565, RGBA4444, BGRA4444, A8, L8, INDEX_8
- **Filters**: POINT, LINEAR, BI_LINEAR
- **VLC path opcodes**: MOVE/LINE/QUAD/CUBIC (absolute + relative), END (auto-close)

### Two Blit Paths

| Path | Shader | MSAA | Blend | Supported Targets |
|------|--------|------|-------|-------------------|
| Native blend | `blit_native.frag` | No | Vulkan pipeline blend | BGRA8888, BGR565 |
| Shader blend | `blit.frag` | 4x | Shader-based | All formats |

Native blend is used for NONE and SRC_OVER on BGRA8888/BGR565 targets. All other format/blend combinations fall back to shader blend.

## Build

Requirements:
- CMake 3.16+
- Vulkan SDK (or llvmpipe software renderer)
- glslangValidator (for SPIR-V shader compilation)
- Python 3 (for SPV header generation, optional)

```bash
./build.sh          # Build
./build.sh clean    # Clean rebuild
./build.sh test     # Build and run tests with Vulkan validation layer
./build.sh run      # Build and run tests
```

### Regenerating Shader SPV Headers

When modifying shaders, see `docs/spv-regeneration.md` for the complete guide.

## Tests

| Test | Description | Status |
|------|-------------|--------|
| test_clear | Full buffer clear with golden verification | PASS (100%) |
| test_clear_unit | Clear unit test with expected buffer | PASS (100%) |
| test_clear_dl | 1920x1080 RGB565 clear | PASS |
| test_align16 | 16-pixel alignment check | PASS |
| test_draw_image | 2 cases: src/dst formats × image modes | PASS |
| test_recolor | RECOLOR mode with rotate/scale/translate | PASS |
| test_tiled | Tiled rendering test | PASS |
| test_gfx1 | Full buffer clear | PASS |
| test_gfx2 / test_gfx3 | Scale/rotate path draw | PASS |
| test_gfx21 | Golden image verification | PASS |
| test_blend_premultiply | Premultiply SRC_OVER blend | PASS |
| test_patternFill | Pattern fill with image transform | PASS |
| test_imgIndex | INDEX_8 CLUT blit | PASS |
| test_sft_clear | 3 cases: rectangle clear, multi-clear | PASS |
| test_tiger | Tiger vector rendering with golden comparison | PASS |
| test_linearGrad | Linear gradient with CPU-vs-GPU verification | PASS (153600/153600 = 100%) |
| test_gradient | 5 blend modes + 18 color count variations | PASS (23/23) |
| test_scissor | Scissor clip test: clear + draw within scissor region | PASS |
| test_radialGrad | Radial gradient rendering | PASS |
| test_imgA8 | A8 source image blit | FAIL (pre-existing, alpha mismatch) |
| test_rotate | Rotate blit (RGB565) | FAIL (pre-existing, golden mismatch) |
| test_scale | Scale blit with golden comparison | FAIL (pre-existing, golden mismatch) |
| test_sft_blit | Full blend mode coverage | FAIL (pre-existing, crash) |

**Summary: 20 PASS / 4 FAIL (pre-existing)**

## Expected Buffer Tracker

The test framework includes a CPU-side `vg_lite_expected_buffer_t` that mirrors GPU operations for pixel-accurate verification:

```c
vg_lite_expected_buffer_t *eb = vg_lite_expected_create(width, height, format);
vg_lite_expected_clear(eb, rect, color);
vg_lite_expected_blit(eb, &src, &matrix, blend, filter, image_mode, flags, color, clut);
vg_lite_expected_draw_grad(eb, &path, fill_rule, &path_matrix, &grad_image, &grad_matrix, blend);
int result = vg_lite_expected_verify(eb, &gpu_buf, tolerance);
vg_lite_expected_destroy(eb);
```

`vg_lite_expected_draw_grad` provides CPU-side gradient simulation with point-in-polygon fill test, matrix transform chain, LINEAR texture sampling, and blend mode support for verification against GPU output.

## License

Proprietary
