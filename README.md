# vglite_by_vulkan

VGLite API implementation based on Vulkan. Provides `vg_lite_clear` and `vg_lite_blit` functionality using Vulkan compute/graphics pipeline, compatible with the VGLite API interface.

## Architecture

```
inc/vg_lite.h          - VGLite public API declarations
inc/vg_lite_util.h     - Utility API declarations
src/vg_lite.c          - Main API implementation (clear, blit, init, finish)
src/vg_lite_vulkan.c   - Vulkan device/context management
src/vg_lite_shader.c   - Vulkan shader module management
src/vg_lite_format.c   - Pixel format conversion utilities
shaders/blit.vert      - Blit vertex shader (SPIR-V)
shaders/blit.frag      - Blit fragment shader with matrix transform
util/util.c            - Test utility: expected buffer tracker for pixel verification
```

## Supported Features

- **vg_lite_init / vg_lite_close** - Vulkan device initialization and cleanup
- **vg_lite_allocate / vg_lite_free** - Buffer allocation via Vulkan memory
- **vg_lite_clear** - Full or rectangle clear with solid color
- **vg_lite_blit** - Blit with 3x3 matrix transform, format conversion, blend modes
- **Blend modes**: NONE, SRC_OVER, PREMULTIPLY_SRC_OVER
- **Pixel formats**: RGBA8888, BGRA8888, RGBX8888, BGRX8888, RGB565, BGR565, RGBA4444, BGRA4444, A8, L8

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

## Tests

11 test cases covering clear, blit, rotate, scale, blend, and format conversion:

| Test | Description | Verification |
|------|-------------|--------------|
| test_clear | Rectangle clear | Full buffer pixel verification |
| test_clear_unit | Unit test clear | Full buffer pixel verification |
| test_clear_dl | Display list clear | Full buffer pixel verification |
| test_gfx1 | Full buffer clear | Full buffer pixel verification |
| test_gfx2 | Scale blit | CPU expected buffer comparison |
| test_gfx3 | Rotate + scale blit | CPU expected buffer comparison |
| test_gfx21 | -90 degree rotate blit | CPU expected buffer comparison |
| test_rotate | Rotate blit (RGB565) | CPU expected buffer comparison |
| test_scale | Multiple scale factors | CPU expected buffer comparison |
| test_blend_premultiply | Premultiply SRC_OVER blend | Full buffer formula verification |
| test_align16 | 16-pixel alignment | Inline pixel checks |

## Expected Buffer Tracker

The test framework includes a CPU-side `vg_lite_expected_buffer_t` that mirrors GPU operations for pixel-accurate verification:

```c
vg_lite_expected_buffer_t *eb = vg_lite_expected_create(width, height, format);
vg_lite_expected_clear(eb, rect, color);           // mirrors vg_lite_clear
vg_lite_expected_blit(eb, &src, &matrix, blend, filter); // mirrors vg_lite_blit
vg_lite_expected_copy(eb, &buf);                    // copy from GPU buffer memory
int result = vg_lite_expected_verify(eb, buf.memory, tolerance); // compare
vg_lite_expected_destroy(eb);
```

## Known Issues

- GPU sampler always uses `VK_FILTER_LINEAR`, ignoring POINT/BI_LINEAR filter parameter. This causes ~1-5% pixel mismatch on rotate/scale tests due to CPU using POINT sampling while GPU uses LINEAR.

## License

Proprietary
