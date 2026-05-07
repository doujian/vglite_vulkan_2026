# VGLite API Reference: `vg_lite_clear` and `vg_lite_blit`

> Sources: Infineon *Vivante Programming: VGLite Vector Graphics API Reference Manual* (002-39840 Rev. \*B, 2025),
> NXP *AN14210* — Learning VGLite API Programming on i.MX RT Series (Rev 2.0, Nov 2025),
> NXP MCUXpresso SDK 25.12 VGLite documentation, VGLite header `vg_lite.h` (v4.0.90).

---

## 1 `vg_lite_clear`

### 1.1 Signature

```c
vg_lite_error_t vg_lite_clear(
    vg_lite_buffer_t    *target,
    vg_lite_rectangle_t *rect,
    vg_lite_color_t      color
);
```

### 1.2 Description

Fills the entire target buffer — or a rectangular sub-region of it — with a solid color. This is the fastest way to initialise or clear a render target.

When the target buffer format is **L8** (luminance-only), the RGBA `color` is automatically converted to a luminance value using the ITU-R BT.709 formula.

### 1.3 Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `vg_lite_buffer_t *` | Destination buffer. All formats in `vg_lite_buffer_format_t` are valid. |
| `rect` | `vg_lite_rectangle_t *` | Area to fill. **NULL** → fill the entire buffer. |
| `color` | `vg_lite_color_t` | Fill colour in **ARGB8888** layout (`0xAARRGGBB`). |

### 1.4 Related structure — `vg_lite_rectangle_t`

```c
typedef struct vg_lite_rectangle {
    vg_lite_int32_t x;       // left coordinate (pixels)
    vg_lite_int32_t y;       // top coordinate  (pixels)
    vg_lite_int32_t width;   // width  (pixels)
    vg_lite_int32_t height;  // height (pixels)
} vg_lite_rectangle_t;
```

### 1.5 Example

```c
vg_lite_error_t error;
vg_lite_buffer_t fb;

/* Init VGLite — tessellation size 0 means only clear/blit are usable */
error = vg_lite_init(0, 0);

/* Allocate a 256×256 RGB565 render target */
fb.width  = 256;
fb.height = 256;
fb.format = VG_LITE_RGB565;
error = vg_lite_allocate(&fb);

/* Fill entire buffer with blue  (ARGB: A=0xFF, R=0x00, G=0x00, B=0xFF) */
error = vg_lite_clear(&fb, NULL, 0xFFFF0000);

/* Fill a 64×64 rectangle at (64,64) with red (ARGB: 0xFF0000FF) */
vg_lite_rectangle_t rect = { 64, 64, 64, 64 };
error = vg_lite_clear(&fb, &rect, 0xFF0000FF);

/* Flush and wait for GPU */
error = vg_lite_finish();

/* Clean up */
error = vg_lite_free(&fb);
error = vg_lite_close();
```

---

## 2 `vg_lite_blit`

### 2.1 Signature

```c
vg_lite_error_t vg_lite_blit(
    vg_lite_buffer_t  *target,
    vg_lite_buffer_t  *source,
    vg_lite_matrix_t  *matrix,
    vg_lite_blend_t    blend,
    vg_lite_color_t    color,
    vg_lite_filter_t   filter
);
```

### 2.2 Description

Copies a source image into a destination buffer through a 3×3 transformation matrix that may include **translation, rotation, scaling, and perspective correction**.

The operation pipeline per pixel is:

1. **Transform** — source pixel coordinates are mapped through `matrix`.
2. **Filter** — the sampling filter (point / linear / bi-linear / gaussian) is applied.
3. **Mix colour** — if `color != 0`, each source pixel is multiplied by `color` (per-channel ARGB multiply) before blending. **Has no effect** when `source->image_mode` is `VG_LITE_ZERO` or `VG_LITE_NORMAL_IMAGE_MODE`.
4. **Blend** — the (optionally mixed) source pixel is blended onto the destination pixel according to `blend`.

> **Note** — `vg_lite_blit` does **not** support coverage-sample anti-aliasing. Rotated or scaled images may show jagged edges at buffer boundaries. Use `vg_lite_draw` (path rendering) for high-quality AA (16×, 8×, 4×).

> **Colour conversion** — with an identity matrix and no blend/colour, `vg_lite_blit` can be used purely for pixel-format conversion between source and destination.

### 2.3 Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `vg_lite_buffer_t *` | Destination buffer. See *Image Source Alignment Requirement* for valid destination formats. |
| `source` | `vg_lite_buffer_t *` | Source image. All `vg_lite_buffer_format_t` formats are valid. |
| `matrix` | `vg_lite_matrix_t *` | 3×3 transform from source → target space. **NULL** → identity matrix (copy at origin `(0,0)`). |
| `blend` | `vg_lite_blend_t` | Blend mode. `VG_LITE_BLEND_NONE` (0) for no blending. See **border-culling note** below. |
| `color` | `vg_lite_color_t` | Mix colour (`0xAARRGGBB`). **0** = no mix. Ignored under `VG_LITE_ZERO` / `VG_LITE_NORMAL_IMAGE_MODE` image modes. |
| `filter` | `vg_lite_filter_t` | Sampling filter. `0` = `VG_LITE_FILTER_POINT`. |

#### Border-culling / transparency override

When `blend` is `VG_LITE_BLEND_NONE`, `VG_LITE_BLEND_SRC_IN`, or `VG_LITE_BLEND_DST_IN` and the matrix includes rotation or perspective:

| `gcFEATURE_BIT_VG_BORDER_CULLING` | Driver override |
|---|---|
| **Supported** | `transparency_mode` → `VG_LITE_IMAGE_TRANSPARENT` |
| **Not supported** | `blend` → `VG_LITE_BLEND_SRC_OVER` |

### 2.4 Related structure — `vg_lite_matrix_t`

```c
typedef struct vg_lite_matrix {
    vg_lite_float_t m[3][3];   // 3×3 matrix in [row][column] order
    vg_lite_float_t scaleX;    // horizontal scale factor (auxiliary)
    vg_lite_float_t scaleY;    // vertical   scale factor (auxiliary)
    vg_lite_float_t angle;     // rotation angle             (auxiliary)
} vg_lite_matrix_t;
```

Helper APIs to build matrices:

```c
vg_lite_error_t vg_lite_identity (vg_lite_matrix_t *matrix);
vg_lite_error_t vg_lite_translate(vg_lite_float_t x, vg_lite_float_t y, vg_lite_matrix_t *matrix);
vg_lite_error_t vg_lite_scale   (vg_lite_float_t sx, vg_lite_float_t sy, vg_lite_matrix_t *matrix);
vg_lite_error_t vg_lite_rotate  (vg_lite_float_t degrees, vg_lite_matrix_t *matrix);
```

### 2.5 Example — blit with scale + translate

```c
vg_lite_buffer_t rt;        // 320×480 render target
vg_lite_buffer_t icon;      // 256×256 source icon
vg_lite_matrix_t matrix;

/* Clear background */
vg_lite_clear(&rt, NULL, 0xFFFF0000);  // blue

for (int i = 0; i < 6; i++) {
    /* Build per-icon matrix: scale down then translate */
    vg_lite_identity(&matrix);
    vg_lite_scale(0.5f, 0.5f, &matrix);                   // 256→128 px
    vg_lite_translate(x_pos[i], y_pos[i], &matrix);       // position

    /* Blit with alpha compositing, no mix colour, nearest-neighbour filter */
    vg_lite_blit(&rt, &icon, &matrix,
                 VG_LITE_BLEND_SRC_OVER,
                 0,                         // no mix colour
                 VG_LITE_FILTER_POINT);
}

vg_lite_finish();
```

### 2.6 Example — blit with multiply image mode (glyph rendering)

```c
/* Render a glyph: source is A8 alpha mask, mix colour provides the fill colour */
vg_lite_buffer_t glyph_buf;
glyph_buf.format     = VG_LITE_A8;
glyph_buf.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;

vg_lite_matrix_t mat;
vg_lite_identity(&mat);
vg_lite_translate(x, y, &mat);

/* The 'color' parameter (0xFF00FF00 = opaque green) multiplies with each
   source pixel's alpha, producing green text with correct alpha edges. */
vg_lite_blit(&rt, &glyph_buf, &mat,
             VG_LITE_BLEND_SRC_OVER,
             0xFF00FF00,
             VG_LITE_FILTER_BI_LINEAR);
```

---

## 3 Shared data types

### 3.1 `vg_lite_color_t`

```c
typedef unsigned int vg_lite_color_t;   // 32-bit
```

Layout in memory (little-endian):

```
 31       24 23       16 15        8 7         0
┌───────────┬───────────┬───────────┬───────────┐
│  Alpha A  │  Blue  B  │  Green G  │  Red   R  │
└───────────┴───────────┴───────────┴───────────┘
```

Examples: `0xFFFF0000` = opaque blue, `0xFF00FF00` = opaque green, `0xFF0000FF` = opaque red, `0x80000000` = 50 % transparent black.

### 3.2 `vg_lite_buffer_t`

```c
typedef struct vg_lite_buffer {
    vg_lite_int32_t           width;               // pixels
    vg_lite_int32_t           height;              // pixels
    vg_lite_int32_t           stride;              // bytes per row
    vg_lite_buffer_layout_t   tiled;               // VG_LITE_LINEAR or VG_LITE_TILED
    vg_lite_buffer_format_t   format;              // pixel format
    vg_lite_pointer           handle;              // kernel memory handle
    vg_lite_pointer           memory;              // CPU-accessible pointer
    vg_lite_uint32_t          address;             // GPU address
    vg_lite_memory_pool_t     pool;                // memory pool selector
    vg_lite_yuvinfo_t         yuv;                 // YUV planar info
    vg_lite_image_mode_t      image_mode;          // blit image mode
    vg_lite_transparency_t    transparency_mode;   // OPAQUE or TRANSPARENT
    vg_lite_fc_buffer_t       fc_buffer[3];        // fast-clear buffers
    vg_lite_compress_mode_t   compress_mode;       // DEC compression
    vg_lite_index_endian_t    index_endian;        // index format endianness
    vg_lite_paint_type_t      paintType;           // paint type
    vg_lite_uint8_t           fc_enable;           // enable IM fast clear
    vg_lite_uint8_t           scissor_buffer;      // scissor mask buffer flag
    vg_lite_uint8_t           premultiplied;       // RGB values are α-premultiplied
    vg_lite_uint8_t           apply_premult;       // apply α-premultiply during blit
    struct vg_lite_buffer    *lvgl_buffer;         // SW LVGL blending buffer
    vg_lite_color_t           bg_color;            // background for edge filter
} vg_lite_buffer_t;
```

#### `image_mode` values (affects `color` parameter behaviour in blit)

| Mode | Value | Effect on `color` param |
|------|-------|------------------------|
| `VG_LITE_ZERO` | 0 | `color` ignored — source pixels are zeroed |
| `VG_LITE_NORMAL_IMAGE_MODE` | 0x1F00 | `color` ignored — source pixels pass through unchanged |
| `VG_LITE_MULTIPLY_IMAGE_MODE` | 0x1F01 | `color` **multiplied** per-channel with each source pixel |
| `VG_LITE_STENCIL_MODE` | 0x1F02 | Stencil mode |
| `VG_LITE_NONE_IMAGE_MODE` | 0x1F03 | No image mode |
| `VG_LITE_RECOLOR_MODE` | 0x1F04 | Recolour mode |

### 3.3 `vg_lite_blend_t`

| Enum | Value | Formula (non-premultiplied) |
|------|-------|----------------------------|
| `VG_LITE_BLEND_NONE` | 0 | `S` (no blend) |
| `VG_LITE_BLEND_SRC_OVER` | 1 | `S + D·(1 − Sa)` |
| `VG_LITE_BLEND_DST_OVER` | 2 | `S·(1 − Da) + D` |
| `VG_LITE_BLEND_SRC_IN` | 3 | `S·Da` |
| `VG_LITE_BLEND_DST_IN` | 4 | `D·Sa` |
| `VG_LITE_BLEND_MULTIPLY` | 5 | `S·(1−Da) + D·(1−Sa) + S·D` |
| `VG_LITE_BLEND_SCREEN` | 6 | `S + D − S·D` |
| `VG_LITE_BLEND_DARKEN` | 7 | `min(SRC_OVER, DST_OVER)` |
| `VG_LITE_BLEND_LIGHTEN` | 8 | `max(SRC_OVER, DST_OVER)` |
| `VG_LITE_BLEND_ADDITIVE` | 9 | `S + D` |
| `VG_LITE_BLEND_SUBTRACT` | 10 | `D·(1 − Sa)` |
| `VG_LITE_BLEND_NORMAL_LVGL` | 11 | `S·Sa + D·(1 − Sa)` |
| `VG_LITE_BLEND_ADDITIVE_LVGL` | 12 | `(S + D)·Sa + D·(1 − Sa)` |
| `VG_LITE_BLEND_SUBTRACT_LVGL` | 13 | `(S − D)·Sa + D·(1 − Sa)` |
| `VG_LITE_BLEND_MULTIPLY_LVGL` | 14 | `(S·D)·Sa + D·(1 − Sa)` |

> `S` = source RGB, `D` = destination RGB, `Sa`/`Da` = source/destination alpha.
> LVGL variants force output alpha to `0xFF` (fully opaque).

OpenVG premultiplied blend modes (`OPENVG_BLEND_*`, values `0x2000`+) are also defined — see `vg_lite.h` for the full list.

### 3.4 `vg_lite_filter_t`

| Enum | Value | Description |
|------|-------|-------------|
| `VG_LITE_FILTER_POINT` | 0 | Nearest-neighbour sampling |
| `VG_LITE_FILTER_LINEAR` | 0x1000 | Linear interpolation (horizontal) |
| `VG_LITE_FILTER_BI_LINEAR` | 0x2000 | 2×2 bilinear interpolation |
| `VG_LITE_FILTER_GAUSSIAN` | 0x3000 | 3×3 Gaussian blur convolution (requires `gcFEATURE_BIT_VG_GAUSSIAN_BLUR`) |

### 3.5 `vg_lite_error_t`

| Enum | Description |
|------|-------------|
| `VG_LITE_SUCCESS` | Operation succeeded |
| `VG_LITE_INVALID_ARGUMENT` | Bad parameter value |
| `VG_LITE_OUT_OF_MEMORY` | GPU memory exhaustion |
| `VG_LITE_NO_CONTEXT` | No / uninitialised context |
| `VG_LITE_TIMEOUT` | Wait timed out |
| `VG_LITE_OUT_OF_RESOURCES` | System resource exhaustion |
| `VG_LITE_GENERIC_IO` | Kernel driver communication failure |
| `VG_LITE_NOT_SUPPORT` | Feature unsupported by hardware |
| `VG_LITE_ALREADY_EXISTS` | Object already exists |
| `VG_LITE_NOT_ALIGNED` | Data alignment error |
| `VG_LITE_FLEXA_TIME_OUT` | FlexA segment-buffer timeout |
| `VG_LITE_FLEXA_HANDSHAKE_FAIL` | FlexA synchroniser handshake failure |

---

## 4 Typical usage pattern

```
┌───────────────────┐
│  vg_lite_init()   │  ← once, at application start
└────────┬──────────┘
         │
┌────────▼──────────┐
│ vg_lite_allocate()│  ← create render target(s)
└────────┬──────────┘
         │
┌────────▼──────────┐
│  vg_lite_clear()  │  ← fill background
└────────┬──────────┘
         │
┌────────▼──────────┐   ┌──────────────────────┐
│  vg_lite_blit()   │ ←─│ vg_lite_identity()    │
│  (repeat as needed)│   │ vg_lite_translate()   │
└────────┬──────────┘   │ vg_lite_scale()       │
         │              │ vg_lite_rotate()       │
┌────────▼──────────┐   └──────────────────────┘
│ vg_lite_finish()  │  ← flush + wait for GPU
└────────┬──────────┘
         │
┌────────▼──────────┐
│  vg_lite_free()   │  ← release buffer memory
└────────┬──────────┘
         │
┌────────▼──────────┐
│  vg_lite_close()  │  ← destroy context
└───────────────────┘
```

> **Tip:** `vg_lite_init(0, 0)` allocates no tessellation buffer, so only `vg_lite_clear` and `vg_lite_blit` are available (no `vg_lite_draw`). Use `vg_lite_init(width, height)` with valid tessellation dimensions to enable path rendering alongside blit operations.
