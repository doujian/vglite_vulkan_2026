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
shaders/radial.vert      - Radial gradient vertex shader (dedicated pipeline)
shaders/radial.frag      - Radial gradient fragment shader (GPU g = gLin + sqrt(gRad) + [0,1] spread + 1D LUT)
shaders/upload_tiled.comp - Unified tiled upload compute shader (src SSBO byte reads -> formatless uimage2D imageStore, requires shaderStorageImageWriteWithoutFormat)
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
- **Pixel formats**: RGBA8888, BGRA8888, ARGB8888, ABGR8888, RGBX8888, BGRX8888, RGB565, BGR565, RGBA4444, BGRA4444, RGBA5551, BGRA5551, ARGB1555, ABGR1555, A8, A4, L8, INDEX_8, OPENVG_sRGBA_8888 (OpenVG MSB-first sRGBA, layout = ABGR8888, VK _SRGB auto sRGB→linear decode on sample)
- **Filters**: POINT, LINEAR, BI_LINEAR
- **VLC path opcodes**: MOVE/LINE/QUAD/CUBIC (absolute + relative), END (auto-close)

### Blit Path

All blits use the native path: `blit_native.frag` (OBB quad) / `blit_native_fs.frag` (fullscreen triangle) with Vulkan hardware pipeline blend (e.g. SRC_OVER = ONE, ONE_MINUS_SRC_ALPHA) + target seeding. Pipelines are cached per (VkFormat, blend group); render passes are generic per format. A seed draw copies the target's content into the 4x MSAA attachment at the start of each new render pass so hardware blend reads the correct dst (needed when the target was filled externally, e.g. CPU-loaded via `vg_lite_load_raw`). Single-channel targets are handled by shader output flags (A8 -> alpha, L8 -> luminance) and seed through identity views. The legacy shader-blend path (`blit.frag`) is currently disabled in code.

### Delayed Clear Optimization

Fullscreen `vg_lite_clear` (rect==NULL or covers entire target) is deferred — no GPU operations are executed immediately. The clear color is stored as pending state on the target buffer. When the next `vg_lite_blit` or `vg_lite_draw` targets the same buffer, the clear is merged into the render pass begin:

- **no-MSAA path**: clear and blit/draw share a single render pass (saves 1 RP open/close).
- **MSAA path**: pending clear is flushed to the target via a no-MSAA RP, then normal `seed_msaa` follows. (Cannot merge into MSAA RP due to llvmpipe `vkCmdClearAttachments` bug on 4x MSAA B5G6R5 attachments.)
- **Flush points**: `vg_lite_finish` and `vg_lite_buffer_read_ptr` automatically flush any unconsumed pending clear.
- **Partial clear** (rect != fullscreen): unchanged — executes immediately via `vkCmdClearAttachments`.

### Runtime MSAA Sample Count (2x / 4x)

The MSAA sample count is runtime-configurable between 2x and 4x (default 4x):

- `vg_lite_set_msaa_samples(2|4)` — public API. Switching tears down all cached MSAA render passes, per-buffer MSAA attachments (tracked via a buffer registry) and pipelines; they are rebuilt lazily on the next draw. Requests are clamped to what the device supports (`framebufferColor/Depth/StencilSampleCounts`).
- Env var `VGLITE_MSAA_SAMPLES=2|4` — selects the sample count at `vg_lite_init` time.
- Render passes, MSAA attachments and pipeline multisample state all read the global `g_msaa_samples`; shaders have no sample-count assumptions.


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

### Buffer Tiling Mode

The backend supports two image tiling modes, controlled by `VGLITE_TARGET_OPTIMAL`:

| Mode | Flag | Memory Type | Description |
|------|------|------------|-------------|
| LINEAR (default) | `-DVGLITE_TARGET_OPTIMAL=OFF` | HOST_VISIBLE | CPU-direct pointer access, original behavior |
| OPTIMAL | `-DVGLITE_TARGET_OPTIMAL=ON` | DEVICE_LOCAL | GPU-private memory, staging transfer for CPU access |

```bash
# LINEAR mode (default)
cmake -B build -DVGLITE_TARGET_OPTIMAL=OFF
cmake --build build

# OPTIMAL mode
cmake -B build_tiled -DVGLITE_TARGET_OPTIMAL=ON
cmake --build build_tiled
```

In OPTIMAL mode, CPU access to buffer pixels goes through staging buffers via:
- `vg_lite_buffer_write(buf, src)` — upload
- `vg_lite_buffer_download(buf, dst)` — download
- `vg_lite_buffer_read_ptr(buf)` — cached read-only pointer

Test PNG outputs are automatically routed to `dump_linear/` or `dump_optimal/` subdirectories.

### Shader System

Shaders are compiled from `shaders/*.vert`, `shaders/*.frag` and `shaders/*.comp` to SPIR-V `.spv` files at build time (output: `build/spv/`). At runtime, `shader_loader.c` loads `.spv` files via `load_shader_module()` with multi-path search:

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
| test_draw_image | 475 cases: 5x5 src/tgt format matrix x image modes x filters x blends (NONE, SRC_OVER) | PASS (0 pixel failures) |
| test_recolor | RECOLOR mode with rotate/scale/translate | PASS |
| test_tiled | Tiled rendering test | PASS |
| test_gfx1 | Full buffer clear | PASS |
| test_gfx2 / test_gfx3 | Scale/rotate path draw | PASS |
| test_gfx21 | Golden image verification | PASS |
| test_blend_premultiply | Premultiply SRC_OVER blend | PASS |
| test_patternFill | Pattern fill with image transform | PASS |
| test_imgIndex | INDEX_8 CLUT blit | PASS |
| test_uploadBuffer | vg_lite_upload_buffer (BGRA8888/RGB565/L8, odd user stride; LINEAR buffers go through the staging + CopyBufferToImage path) | PASS |
| test_uploadTiled | vg_lite_upload_buffer into TILED (OPTIMAL) buffers: single formatless-imageStore compute path, verified identical to linear upload (BGRA8888/RGBA8888/RGB565/L8) | PASS |
| test_sft_clear | 3 cases: rectangle clear, multi-clear | PASS |
| test_tiger | Tiger vector rendering with golden comparison | PASS |
| test_linearGrad | Linear gradient with CPU-vs-GPU verification | PASS (153600/153600 = 100%) |
| test_gradient | 5 blend modes + 18 color count variations | PASS (23/23) |
| test_scissor | Scissor clip test: clear + draw within scissor region | PASS |
| test_radialGrad | Radial gradient, 4 spread modes (PAD/REPEAT/REFLECT/FILL) | PASS (307200/307200 each) |
| test_imgA8 | A8 source image blit | PASS |
| test_imgA4 | A4 packed alpha mask blit (GPU-expanded to R8) | PASS |
| test_openvg_srgba | OPENVG_sRGBA_8888 source blit (BLEND_NONE + SRC_OVER vs CPU model) | PASS |
| test_optimal_roundtrip | landscape.raw upload (OPTIMAL) → download → byte-exact compare + png dump | PASS (480000/480000, 8 configs) |
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
| test_uploadBatch | vg_lite_upload_buffers batch API: mixed 6-buffer batch (linear/tiled × formats), single staging + single submit, byte-exact download compare | PASS |
| test_msaaSwitch | Runtime MSAA 2x/4x switching (vg_lite_set_msaa_samples): pipeline/attachment invalidation, draw across switches | PASS |

**Summary: 43 PASS / 1 FAIL**

Note: on some machines test_gfx3 and test_imgIndex also fail locally (pre-existing, unrelated to current HEAD).

Tiled buffers: `vg_lite_allocate` with `buffer->tiled = VG_LITE_TILED` (single-plane >=8bpp formats) creates a `VK_IMAGE_TILING_OPTIMAL`, device-local, unmapped image. The creation combo is decided once at allocate time by a capability probe chain, so upload never re-probes: (1) if the device supports OPTIMAL+STORAGE+MUTABLE_FORMAT, the image carries STORAGE usage (packed 16bpp images are created as R16_UINT views-compat so STORAGE is available, sampling views keep the original format) and `vg_lite_upload_buffer` fills it via the single `shaders/upload_tiled.comp`: user rows are packed into a staging SSBO, the compute shader reads bytes/halfwords/words per `bytes_per_pixel` and writes them through a formatless `uimage2D` (R32/R16/R8_UINT storage view, requires `shaderStorageImageWriteWithoutFormat`) — the hardware resolves tile addressing; 32bpp, 16bpp (RGB565 family) and 8bpp formats are supported. (2) On devices without OPTIMAL+STORAGE support the image is created in the real format with base usage and upload goes through a staging + `vkCmdCopyBufferToImage` path. (3) If even base OPTIMAL is rejected, allocation degrades to LINEAR. Set `VGLITE_DISABLE_STORAGE_UPLOAD=1` to force path (2) for testing.

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
