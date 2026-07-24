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
src/shader_loader.c      - Runtime .spv file loader (multi-path search, replaces embedded headers)
src/shader_loader.h      - Shader loader API: load_shader_module()
shaders/blit.vert        - Blit vertex shader (full-screen triangle)
shaders/blit_obb.vert    - Blit vertex shader (OBB quad, TRIANGLE_STRIP 4 verts from push constants)
shaders/blit.frag        - Blit fragment shader (shader-blend path, MSAA)
shaders/blit_native.frag - Blit fragment shader (hardware-blend path, 4x MSAA + seed)
shaders/draw.vert        - Draw vertex shader
shaders/draw.frag        - Draw fragment shader
shaders/gradient.vert    - Linear gradient vertex shader
shaders/gradient.frag    - Linear gradient fragment shader
shaders/pattern.vert     - Pattern fill vertex shader
shaders/pattern.frag     - Pattern fill fragment shader
util/util.c              - Test utility: expected buffer, gen_image, pack/read pixel, CPU gradient sim
util/vg_lite_util.c      - PNG save/load, buffer allocation helper
util/Common.h            - Shared test macros (CHECK_ERROR, IS_ERROR)
docs/vg_lite_draw.md     - vg_lite_draw API documentation
```

## Supported Features

- **vg_lite_init / vg_lite_close** - Vulkan device initialization and cleanup
- **vg_lite_allocate / vg_lite_free** - Buffer allocation via Vulkan memory
- **vg_lite_clear** - Full or rectangle clear with solid color
- **vg_lite_blit** - Blit with 3x3 matrix transform, format conversion, blend modes
- **OBB blit optimization** - Dynamic vertex shader computes tight quad from source OBB, reducing rasterized fragments by up to 17x for small sources (runtime switch via `vg_lite_set_blit_obb_mode()`)
- **vg_lite_draw** - Path fill with tessellation (even-odd fill rule; blend modes via per-blend cover pipeline)
- **vg_lite_init_path** - Programmatic path creation (bounding box, quality, format, data)
- **vg_lite_draw_grad** - Linear gradient fill with dedicated Vulkan shaders
- **vg_lite_draw_radial_grad** - Radial gradient fill
- **vg_lite_draw_pattern** - Pattern image fill with transform and blend
- **vg_lite_set_CLUT** - 256-entry color lookup table for INDEX_8 format
- **Scissor clip** - vg_lite_set_scissor, vg_lite_scissor_rects (multi-rect), enable/disable
- **Matrix ops** - identity, translate, scale, rotate
- **Blend modes**: NONE, SRC_OVER, DST_OVER, SRC_IN, DST_IN, MULTIPLY, SCREEN, DARKEN, LIGHTEN, ADDITIVE, SUBTRACT, NORMAL_LVGL, ADDITIVE_LVGL, SUBTRACT_LVGL, MULTIPLY_LVGL, OpenVG premultiplied modes
- **Image modes**: NONE (color only), NORMAL, MULTIPLY, STENCIL, RECOLOR
- **Pixel formats**: RGBA8888, BGRA8888, ARGB8888, ABGR8888, RGBX8888, BGRX8888, RGB565, BGR565, RGBA4444, BGRA4444, A8, L8, INDEX_8
- **Filters**: POINT, LINEAR, BI_LINEAR
- **VLC path opcodes**: MOVE/LINE/QUAD/CUBIC (absolute + relative), END (auto-close)

### Two Blit Paths

| Path | Shader | MSAA | Blend | Supported Targets |
|------|--------|------|-------|-------------------|
| Native blend | `blit_native.frag` | 4x | Vulkan hardware pipeline blend + target seeding | BGRA8888, BGR565, RGBA8888, RGB565, A8, L8 |
| Shader blend | `blit.frag` | 4x | Shader-based (temp copy for dst) | All formats |

Native blend is used for NONE/SRC_OVER/DST_OVER/ADDITIVE/SUBTRACT on common formats. It uses 4x MSAA with hardware pipeline blend. A seed draw copies the target's content into the MSAA at the start of each new render pass, so hardware blend reads the correct dst (needed when the target was filled externally, e.g. CPU-loaded via `vg_lite_load_raw`). All other format/blend combinations use shader blend (also 4x MSAA, but computes blend in the shader with a temp copy of the target as dst).

## Build

Requirements:
- CMake 3.16+
- Vulkan SDK (or llvmpipe software renderer)
- glslangValidator (for SPIR-V shader compilation)

```bash
./build.sh          # Build
./build.sh clean    # Clean rebuild
./build.sh test     # Build and run tests with Vulkan validation layer
./build.sh run      # Build and run tests
```

### Shader System

Shaders are compiled from `shaders/*.vert` and `shaders/*.frag` to SPIR-V `.spv` files at build time (output: `build/spv/`). At runtime, `shader_loader.c` loads `.spv` files via `load_shader_module()` with multi-path search:

1. `SPV_SEARCH_PATH` environment variable
2. `./spv/` (current working directory)
3. `<exe_dir>/spv/`
4. `<exe_dir>/../spv/`
5. `<exe_dir>/../../spv/`

This allows shader modifications without recompiling C code �?just rebuild shaders and rerun.

## Tests

| Test | Description | Status |
|------|-------------|--------|
| test_clear | Full buffer clear with golden verification | PASS (100%) |
| test_clear_unit | Clear unit test with expected buffer | PASS (100%) |
| test_clear_dl | 1920x1080 RGB565 clear | PASS |
| test_align16 | 16-pixel alignment check | PASS |
| test_draw_image | 72 cases: src/dst formats × image modes | PASS |
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
| test_radialGrad | Radial gradient, 4 spread modes (PAD/REPEAT/REFLECT/FILL) | PASS (307200/307200 each) |
| test_imgA8 | A8 source image blit | PASS |
| test_rotate | Rotate blit (RGB565) | PASS (fixed: discard out-of-bounds UVs) |
| test_scale | Scale blit with golden comparison | PASS |
| test_blit_multi | Multiple blits to single target | PASS |
| test_blit_accum | Blit accumulation (deferred batching) | PASS |
| test_blit_chain | Sequential blit chain A→B→C | PASS |
| test_blit_mixed | Mixed format blits to shared target | PASS |
| test_blit_switch | 9 blits to A, then blit A→B | PASS |
| test_blit_perf | OBB vs fullscreen perf comparison (GPU timestamps) | PASS |
| test_blit_draw | Blit→draw seed_msaa RP transition | PASS |
| test_multi_draw | Multi-path draw with gradient (RGB565→BGR565 fallback on Windows) | PASS |
| test_bgr565_clear | BGR565 color encoding self-check (skips RGB565 if unsupported) | PASS |
| test_glyphs2 | CTS glyphs rendering | PASS |
| test_sft_blit | Full blend mode coverage | FAIL (pre-existing, crash) |
| test_vector | CTS vector polygon (256x256, golden .raw compare) | PASS (100%) |
| test_clock | CTS clock face (320x480, golden .raw compare) | PASS (100%) |
| test_ui | CTS ui icons + translucent highlight (golden .raw compare) | PASS (100%) |

**Summary: 36 PASS / 1 FAIL**

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
