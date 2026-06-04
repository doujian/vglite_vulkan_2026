# vglite_by_vulkan

VGLite API implementation based on Vulkan. Provides `vg_lite_clear`, `vg_lite_blit`, `vg_lite_draw`, and related functionality using Vulkan graphics pipeline, compatible with the VGLite API interface.

## Architecture

```
inc/vg_lite.h            - VGLite public API declarations
inc/vg_lite_util.h       - Utility API declarations
src/vg_lite.c            - Main API implementation (clear, blit, init, finish, matrix)
src/vg_lite_vulkan.c     - Vulkan device/context/pipeline management
src/vg_lite_draw.c       - Path drawing (fill, stroke, pattern)
src/vg_lite_format.c     - Pixel format conversion (format→Vulkan mapping, bpp)
src/vg_lite_math.h       - 3x3 matrix helpers (mat3_multiply, mat3_inverse)
src/vlc_parser.c         - VLC path data parser
src/tessellator.c        - Path tessellation (triangulation)
shaders/blit.vert        - Blit vertex shader (full-screen triangle)
shaders/blit.frag        - Blit fragment shader (shader-blend path, MSAA)
shaders/blit_native.frag - Blit fragment shader (pipeline-blend path, no MSAA)
shaders/draw.vert        - Draw vertex shader
shaders/draw.frag        - Draw fragment shader
util/util.c              - Test utility: expected buffer, gen_image, pack/read pixel
util/vg_lite_util.c      - PNG save/load, buffer allocation helper
util/Common.h            - Shared test macros (CHECK_ERROR, IS_ERROR)
docs/spv-regeneration.md - SPV shader rebuild guide and troubleshooting
```

## Supported Features

- **vg_lite_init / vg_lite_close** - Vulkan device initialization and cleanup
- **vg_lite_allocate / vg_lite_free** - Buffer allocation via Vulkan memory
- **vg_lite_clear** - Full or rectangle clear with solid color
- **vg_lite_blit** - Blit with 3x3 matrix transform, format conversion, blend modes
- **vg_lite_draw** - Path fill with tessellation (non-zero / even-odd fill rules)
- **vg_lite_set_CLUT** - 256-entry color lookup table for INDEX_8 format
- **Matrix ops** - identity, translate, scale, rotate
- **Blend modes**: NONE, SRC_OVER, DST_OVER, SRC_IN, DST_IN, MULTIPLY, SCREEN, DARKEN, LIGHTEN, ADDITIVE, SUBTRACT, NORMAL_LVGL, ADDITIVE_LVGL, SUBTRACT_LVGL, MULTIPLY_LVGL, OpenVG premultiplied modes
- **Image modes**: NONE (color only), NORMAL, MULTIPLY, STENCIL, RECOLOR
- **Pixel formats**: RGBA8888, BGRA8888, RGBX8888, BGRX8888, RGB565, BGR565, RGBA4444, BGRA4444, A8, L8, INDEX_8
- **Filters**: POINT, LINEAR, BI_LINEAR

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
| test_draw_image | 76 cases: src/dst formats × image modes × filters × blend modes | 76/76 PASS |
| test_recolor | 6 cases: RECOLOR mode with rotate/scale/translate | 6/6 PASS |
| test_sft_clear | 3 cases: rectangle clear, multi-clear | 3/3 PASS |
| test_clear | Full buffer clear with golden verification | PASS |
| test_scale | 8 scale factors | 8/8 PASS |
| test_gfx1 | Full buffer clear | PASS |
| test_blend_premultiply | Premultiply SRC_OVER blend | PASS |
| test_imgA8 | A8 source image blit | PASS |
| test_rotate | Rotate blit (RGB565) | ~99% (1px edge rounding) |
| test_imgIndex | INDEX_8 CLUT blit | ~99% (pre-existing mismatch) |
| test_gfx2 / test_gfx3 | Scale/rotate path draw | FAIL (pre-existing, draw-related) |
| test_sft_blit | Full blend mode coverage | FAIL (pre-existing, many blend modes unimplemented) |

## Expected Buffer Tracker

The test framework includes a CPU-side `vg_lite_expected_buffer_t` that mirrors GPU operations for pixel-accurate verification:

```c
vg_lite_expected_buffer_t *eb = vg_lite_expected_create(width, height, format);
vg_lite_expected_clear(eb, rect, color);
vg_lite_expected_blit(eb, &src, &matrix, blend, filter, image_mode, flags, color, clut);
int result = vg_lite_expected_verify(eb, &gpu_buf, tolerance);
vg_lite_expected_destroy(eb);
```

## License

Proprietary
